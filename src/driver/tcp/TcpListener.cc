#include "driver.h"
#include "sockets.h"
#include "utils.h"
#include "tcp.h"
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string>
#include <list>
#include <map>

class TcpListener : public driver<TcpListener> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_TCP);
		idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
		TcpPacket* packet = static_cast<TcpPacket*>(message->data);
		assert(packet->type == TcpPacket::kListen);
		uint32_t session = packet->session;

		// printf("ip   = %s\n", packet->listen.ip);
		// printf("port = %d\n", packet->listen.port);
		// printf("ipv6 = %d\n", packet->listen.ipv6);

		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
		// gmtime_r(&now, &tm);
		strftime(timebuf, sizeof timebuf, "%Y.%m.%d-%H:%M:%S", &tm);
		start_ = timebuf;

		struct sockaddr_in6 addr6;
		struct sockaddr_in  addr4;
		struct sockaddr* addr = NULL;
		bzero(&addr6, sizeof(addr6));
		bzero(&addr4, sizeof(addr4));
		
		int ok = 0;
		fd_ = sockets::createTcpNonblockingOrDie(packet->listen.ipv6? AF_INET6 : AF_INET);
		sockets::setReuseAddr(fd_, 1);
		sockets::setReusePort(fd_, 1);
		if (packet->listen.ipv6) {
			addr = sockets::fromIpPort(packet->listen.ip, packet->listen.port, &addr6);
		} else {
			addr = sockets::fromIpPort(packet->listen.ip, packet->listen.port, &addr4);
		}
		if ((ok = sockets::bind(fd_, addr)) == 0) {
			 ok = sockets::listen(fd_);
		}
		packet  = (TcpPacket*)malloc(sizeof(TcpPacket));
		packet->type = TcpPacket::kStatus;
		packet->id = port->id();
		packet->session = session;
		packet->status.id = port->id();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);

		if (ok == 0) {
			packet->status.online = true;
			PortCtl(port, port->id(), 0, port->owner());
		} else {
			packet->status.online = false;
			port->exit();
		}
		port->send(message->source, msg);
		// fprintf(stderr, "TcpListener id=%d init fd=%d ok=%d\n", port->id(), fd_, ok);
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		port->channel(fd_)->disableAll()->update();
		sockets::close(fd_);
		::close(idleFd_);
		for (auto fd : acceptlist_) {
			::close(fd);
		}
		// fprintf(stderr, "TcpListener id=%d release\n", port->id());
	}

	bool receive(PortPtr port, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_TCP) {
			TcpPacket* packet = static_cast<TcpPacket*>(message->data);
			switch (packet->type) {
				case TcpPacket::kAccept:
					return accept(port, message->source, packet->session);
				case TcpPacket::kClose:
					return close(port, message->source, packet->id);
				case TcpPacket::kOpts:
					return setopts(port, packet);
				case TcpPacket::kInfo:
					return getinfo(port, message->source, packet->session);
				default:
					return true;
			}
		} else if (message->type == MSG_TYPE_EVENT) {
			Event* event = static_cast<Event*>(message->data);
			switch (event->type) {
				case Event::kRead:
					return handleIoReadEvent(port, event->fd);
				case Event::kWrite:
					return true;
				case Event::kError:
					return true;
				default:
					return true;
			}
		} else {
			assert(false);
			return true;
		}
	}
	bool setopts(PortPtr port, TcpPacket* packet)
	{
		// fprintf(stderr, "TcpListener id=%d setopts\n", port->id());

		if (packet->opts.optsbits & 0B00000001) {
			// printf("opts.reuseaddr = %d\n", packet->opts.reuseaddr);
			reuseaddr_ = packet->opts.reuseaddr;
			sockets::setReuseAddr(fd_, reuseaddr_);
		}
		if (packet->opts.optsbits & 0B00000010) {
			// printf("opts.reuseport = %d\n", packet->opts.reuseport);
			reuseport_ = packet->opts.reuseport;
			sockets::setReusePort(fd_, reuseport_);
		}
		if (packet->opts.optsbits & 0B00000100) {
			// printf("opts.keepalive = %d\n", packet->opts.keepalive);
			keepalive_ = packet->opts.keepalive;
		}
		if (packet->opts.optsbits & 0B00001000) {
			// printf("opts.nodelay   = %d\n", packet->opts.nodelay);
			nodelay_ = packet->opts.nodelay;
		}
		if (packet->opts.optsbits & 0B00010000) {
			// printf("opts.active    = %d\n", packet->opts.active);
			active_ = packet->opts.active;
		}
		if (packet->opts.optsbits & 0B00100000) {
			// printf("opts.owner     = %d\n", packet->opts.owner);
			if (port->owner() != packet->opts.owner) {
				PortCtl(port, port->id(), port->owner(), packet->opts.owner);
				port->setOwner(packet->opts.owner);
			}
		}
		if (packet->opts.optsbits & 0B01000000) {
			// printf("opts.read      = %d\n", packet->opts.read);
			read_ = packet->opts.read;
			if (read_) {
				port->channel(fd_)->enableReading()->update();
			} else {
				port->channel(fd_)->disableReading()->update();
			}
		}
		return true;
	}



	bool handleIoReadEvent(PortPtr port, int fd)
	{
		assert(fd_ == fd);
		struct sockaddr_in6 addr6;
		
		int connfd = sockets::accept(fd, &addr6);
		// printf("new connection active=%d,fd=%d\n", active_, connfd);
		if (connfd >= 0) {
			readCount_ += 1;
		}

		if (active_) {
			if (connfd >= 0) {
				newconnection(port, port->owner(), 0, connfd);
			} else if (errno == EMFILE) {
				// Too many open files. No more file descriptors are available, so no more files can be opened.
				::close(idleFd_);
				idleFd_ = ::accept(fd, NULL, NULL);
				::close(idleFd_);
				idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
			}
		} else {
			if (connfd >= 0) {
				acceptlist_.push_front(connfd);
				if (acceptlist_.size() >= 8) {
					port->channel(fd_)->disableReading()->update();
				}
			}
		}
		
		return true;
	}

	bool accept(PortPtr port, uint32_t source, uint32_t session)
	{
		// printf("accept....... start\n");
		assert(port->owner() == source);

		if (read_ && acceptlist_.size() < 2 && !port->channel(fd_)->isReading()) {
			port->channel(fd_)->enableReading()->update();
		}
		if (acceptlist_.empty()) {
			return false;
		}


		int connfd = acceptlist_.back();
		acceptlist_.pop_back();

		// printf("accept....... fd = %d\n", connfd);
		newconnection(port, source, session, connfd);
		return true;
	}
	
	void newconnection(PortPtr port, uint32_t source, uint32_t session, int connfd)
	{
		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		bzero(packet, sizeof(TcpPacket));
		packet->type = TcpPacket::kShift;
		packet->id = 0;
		packet->session = 0;
		packet->shift.fd = connfd;
		uint32_t connid = port->newport("TcpConnection", MSG_TYPE_TCP, packet, sizeof(TcpPacket));
		assert(connid > 0);

		packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		bzero(packet, sizeof(TcpPacket));
		packet->type = TcpPacket::kOpts;
		packet->id = connid;
		packet->session = 0;

		packet->opts.optsbits |= 0B00000001;
		packet->opts.reuseaddr = reuseaddr_;
	
		packet->opts.optsbits |= 0B00000010;
		packet->opts.reuseport = reuseport_;
	
		packet->opts.optsbits |= 0B00000100;
		packet->opts.keepalive = keepalive_;
	
		packet->opts.optsbits |= 0B00001000;
		packet->opts.nodelay   = nodelay_;
	
		packet->opts.optsbits |= 0B00010000;
		packet->opts.active    = active_;
		
		auto optsmsg     = port->makeMessage();
		optsmsg->type    = MSG_TYPE_TCP;
		optsmsg->data    = packet;
		optsmsg->size    = sizeof(TcpPacket);
		port->command(connid, optsmsg);
			

		// int connid = 0;

		struct sockaddr_in6 addr6  = sockets::getPeerAddr(connfd);
		struct sockaddr* addr = (struct sockaddr*)&addr6;

		packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		packet->type = TcpPacket::kAccept;
		packet->id = port->id();
		packet->session = session;

		sockets::toIp(packet->accept.ip, 64, addr);
		packet->accept.port   = sockets::toPort(addr);
		packet->accept.ipv6   = addr->sa_family == AF_INET6;
		packet->accept.id     = connid;

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);

		port->send(source, msg);
	}

	bool close(PortPtr port, uint32_t source, uint32_t id)
	{
		assert(port->owner() == source);
		assert(port->id() == id);
		PortCtl(port, port->id(), port->owner(), 0);
		port->exit();
		return true;
	}
	bool getinfo(PortPtr port, uint32_t source, uint32_t session)
	{
		struct sockaddr_in6 addr6 = sockets::getLocalAddr(fd_);
		struct sockaddr* addr = (struct sockaddr*)&addr6;

		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		memset(packet, 0, sizeof(TcpPacket));

		packet->type    = TcpPacket::kInfo;
		packet->id      = port->id();
		packet->session = session;
		sockets::toIp(packet->info.ip, 64, addr);
		packet->info.port   = sockets::toPort(addr);
		packet->info.ipv6   = addr->sa_family == AF_INET6;

		memcpy(packet->info.start, start_.data(), start_.size());
		packet->info.owner  = port->owner();
		packet->info.readCount = readCount_;
		packet->info.readBuff  = acceptlist_.size();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);
		port->send(source, msg);

		return true;
	}
private:
	int fd_ = 0;
	int idleFd_ = 0;
	
	bool reuseaddr_ = false;
	bool reuseport_ = false;
	bool keepalive_ = false;
	bool nodelay_   = false;
	bool active_    = false;
	bool read_      = false;

	std::list<int> acceptlist_;

	std::string start_;
	uint32_t readCount_  = 0;
};

reg(TcpListener)