#include "driver.h"
#include "sockets.h"
#include <sys/socket.h>
#include "utils.h"
#include "udp.h"


class udp : public driver<udp> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		state_ = kOpening;
		assert(message->type == MSG_TYPE_UDP);
		UdpPacket* packet = static_cast<UdpPacket*>(message->data);
		assert(packet->type == UdpPacket::kOpen);
		uint32_t session = packet->session;
		// printf("open ip   = %s\n", packet->open.ip);
		// printf("open port = %d\n", packet->open.port);
		// printf("open ipv6 = %d\n", packet->open.ipv6);

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
		
		fd_ = sockets::createUdpNonblockingOrDie(packet->open.ipv6? AF_INET6 : AF_INET);
		if (packet->open.ipv6) {
			addr = sockets::fromIpPort(packet->open.ip, packet->open.port, &addr6);
		} else {
			addr = sockets::fromIpPort(packet->open.ip, packet->open.port, &addr4);
		}
		int ok = sockets::bind(fd_, addr);

		packet = (UdpPacket*)malloc(sizeof(UdpPacket));
		bzero(packet, sizeof(UdpPacket));
		packet->type = UdpPacket::kStatus;
		packet->id = port->id();
		packet->session = session;

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_UDP;
		msg->data    = packet;
		msg->size    = sizeof(UdpPacket);

		if (ok == 0) {
			packet->status.online = true;
			state_ = kOpened;
			PortCtl(port, port->id(), 0, port->owner());
		} else {
			packet->status.online = false;
			state_ = kClosing;
			port->exit();
		}
		port->send(message->source, msg);
		// fprintf(stderr, "udp id=%d init ok=%d\n", port->id(), ok);
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		port->channel(fd_)->disableAll()->update();
		sockets::close(fd_);
		// assert(state_ == kClosing);
		state_ = kClosed;
		for (auto& m : inputBuffer_) {
			free(m.first);
		}
		// fprintf(stderr, "udp id=%d release\n", port->id());
	}

	bool receive(PortPtr port, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_UDP) {
			UdpPacket* packet = static_cast<UdpPacket*>(message->data);
			switch (packet->type) {
				case UdpPacket::kSend:
					return send(port, packet);
				case UdpPacket::kRecv:
					return recv(port, message->source, packet->session);
				case UdpPacket::kClose:
					return close(port, message->source, packet->id);
				case UdpPacket::kOpts:
					return setopts(port, packet);
				case UdpPacket::kInfo:
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

	bool handleIoReadEvent(PortPtr port, int fd)
	{
		assert(fd_ == fd);
		char buf[65536];

		struct sockaddr_in6 addr6;
		socklen_t        len  = sizeof(addr6);
		struct sockaddr* addr = (struct sockaddr*)&addr6;

		ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
		
		// printf("n = %zd, active_ = %d\n", n, active_);
		if (n > 0) {
			readCount_ += n;
		}

		if (active_) {
			if (n >= 0) {
				UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket) + n);
				packet->type = UdpPacket::kRecv;
				packet->id = port->id();
				packet->session = 0;
				filladdr(packet, addr);
				packet->recv.nbyte = n;
				memcpy(packet->recv.data, buf, n);

				auto msg  = port->makeMessage();
				msg->type = MSG_TYPE_UDP;
				msg->data = packet;
				msg->size = sizeof(UdpPacket) + n;
				port->send(port->owner(), msg);

				// printf("new msg active true owner = %d\n", port->owner());
			} else {
				switch(errno) {
				case EINTR:
				// case AGAIN_WOULDBLOCK:
					break;
				default: {
						struct sockaddr_in6 addr6 = sockets::getLocalAddr(fd_);
						struct sockaddr* addr = (struct sockaddr*)&addr6;

						UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
						packet->type = UdpPacket::kRecv;
						packet->id = port->id();
						packet->session = 0;
						filladdr(packet, addr);
						packet->recv.nbyte = 0;

						auto msg     = port->makeMessage();
						msg->type    = MSG_TYPE_UDP;
						msg->data    = packet;
						msg->size    = sizeof(UdpPacket);
						port->send(port->owner(), msg);
					}
					break;
				}
			}
		} else {
			if (n >= 0) {
				UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket) + n);
				packet->type = UdpPacket::kRecv;
				packet->id = port->id();
				packet->session = 0;
				filladdr(packet, addr);
				packet->recv.nbyte = n;
				memcpy(packet->recv.data, buf, n);

				inputBuffer_.push_front({packet, sizeof(UdpPacket)+n});
				if (inputBuffer_.size() > 8) {
					port->channel(fd_)->disableReading()->update();
				}
				// printf("new msg active false owner = %d\n", port->owner());
			} else {
				switch(errno) {
				case EINTR:
				// case AGAIN_WOULDBLOCK:
					break;
				default:
					state_ = kClosing;
					break;
				}
			}
		}
		return true;
	}
	bool send(PortPtr port, UdpPacket* packet)
	{
		struct sockaddr_in  addr4;
		struct sockaddr_in6 addr6;
		socklen_t len = 0;
		struct sockaddr* addr = NULL;
		bzero(&addr4, sizeof addr4);
		bzero(&addr6, sizeof addr6);

		// printf("send ip   = %s\n", packet->send.ip);
		// printf("send port = %d\n", packet->send.port);
		// printf("send ipv6 = %d\n", packet->send.ipv6);

		if (packet->send.ipv6) {
			len  = sizeof(addr6);
			addr = sockets::fromIpPort(packet->send.ip, packet->send.port, &addr6);
		} else {
			len  = sizeof(addr4);
			addr = sockets::fromIpPort(packet->send.ip, packet->send.port, &addr4);
		}
		int ok = sendto(fd_, packet->send.data, packet->send.nbyte, 0, addr, len);
		if (ok < 0) {
			writeError_ += packet->send.nbyte;
		} else {
			writeCount_ += packet->send.nbyte;
		}
		// fprintf(stderr, "sendto ok = %d\n", ok);
		return true;
	}

	bool recv(PortPtr port, uint32_t source, uint32_t session)
	{
		assert(port->owner() == source);
		// printf("recv start ...\n");
		
		if (state_ == kClosing && inputBuffer_.size() == 0) {
			// printf("recv close ...\n");
			struct sockaddr_in6 addr6 = sockets::getLocalAddr(fd_);
			struct sockaddr* addr = (struct sockaddr*)&addr6;

			UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
			packet->type = UdpPacket::kRecv;
			packet->id = port->id();
			packet->session = session;
			filladdr(packet, addr);
			packet->recv.nbyte = 0;

			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_UDP;
			msg->data    = packet;
			msg->size    = sizeof(UdpPacket);
			port->send(source, msg);

			return true;
		}

		if (state_ == kOpened && read_ && inputBuffer_.size() < 2 && !port->channel(fd_)->isReading()) {
			port->channel(fd_)->enableReading()->update();
		}

		if (inputBuffer_.size() == 0) {
			return false;
		}
		UdpPacket* packet = (UdpPacket*)(inputBuffer_.back().first);
		uint32_t size = (uint32_t)(inputBuffer_.back().second);
		inputBuffer_.pop_back();

		packet->session = session;
		// printf("recv return msg ...\n");
		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_UDP;
		msg->data    = packet;
		msg->size    = size;

		port->send(port->owner(), msg);
		
		return true;
	}

	bool setopts(PortPtr port, UdpPacket* packet)
	{
		// fprintf(stderr, "udp id=%d setopts\n", port->id());

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
		}
		if (packet->opts.optsbits & 0B00001000) {
			// printf("opts.nodelay   = %d\n", packet->opts.nodelay);
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

	bool close(PortPtr port, uint32_t source, uint32_t id)
	{
		assert(port->owner() == source);
		assert(port->id() == id);
		PortCtl(port, port->id(), port->owner(), 0);
		port->exit();
		state_ = kClosing;
		return true;
	}


	bool getinfo(PortPtr port, uint32_t source, uint32_t session)
	{
		uint32_t readBuff = 0;
		for (auto l : inputBuffer_) {
			UdpPacket* packet = static_cast<UdpPacket*>(l.first);
			readBuff += packet->recv.nbyte;
		}

		struct sockaddr_in6 addr6 = sockets::getLocalAddr(fd_);
		struct sockaddr* addr = (struct sockaddr*)&addr6;

		UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
		memset(packet, 0, sizeof(UdpPacket));
		packet->type = UdpPacket::kInfo;
		packet->id = port->id();
		packet->session = session;
		sockets::toIp(packet->info.ip, 64, addr);
		packet->info.port   = sockets::toPort(addr);
		packet->info.ipv6   = addr->sa_family == AF_INET6;

		memcpy(packet->info.start, start_.data(), start_.size());
		packet->info.readCount = readCount_;
		packet->info.readBuff  = readBuff;
		packet->info.writeCount = writeCount_;
		packet->info.writeError = writeError_; 
		packet->info.owner = port->owner();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_UDP;
		msg->data    = packet;
		msg->size    = sizeof(UdpPacket);
		port->send(source, msg);

		return true;
	}
private:
	void filladdr(UdpPacket* packet, const struct sockaddr* addr)
	{
		sockets::toIp(packet->recv.ip, 64, addr);
		packet->recv.port   = sockets::toPort(addr);
		packet->recv.ipv6   = addr->sa_family == AF_INET6;
	}


	enum State {kOpening, kOpened, kClosing, kClosed};
	State state_;

	int fd_ = -1;

	bool reuseaddr_ = false;
	bool reuseport_ = false;
	bool active_    = false;
	bool read_      = false;
	
	std::list<std::pair<void*,uint32_t>> inputBuffer_;

	std::string start_;
	uint32_t readCount_;
	uint32_t writeCount_;
	uint32_t writeError_;
};

reg(udp)

