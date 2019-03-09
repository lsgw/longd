#include "driver.h"
#include "Buffer.h"
#include "sockets.h"
#include "tcp.h"
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

class TcpConnnection : public driver<TcpConnnection> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_TCP);

		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
		// gmtime_r(&now, &tm);
		strftime(timebuf, sizeof timebuf, "%Y.%m.%d-%H:%M:%S", &tm);
		start_ = timebuf;
		
		state_ = kConnecting;
		TcpPacket* packet = static_cast<TcpPacket*>(message->data);
		assert(packet->type == TcpPacket::kShift);
		assert(packet->shift.fd > 0);

		fd_ = packet->shift.fd;
		state_ = kConnected;

		PortCtl(port, port->id(), 0, port->owner());
		// printf("TcpConnnection id=%d init fd = %d\n", port->id(), packet->shift.fd);
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		port->channel(fd_)->disableAll()->update();
		sockets::close(fd_);
		state_ = kDisconnected;
		// printf("TcpConnnection id=%d release fd = %d\n", port->id(), fd_);
	}

	bool receive(PortPtr port, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_TCP) {
			TcpPacket* packet = static_cast<TcpPacket*>(message->data);
			switch (packet->type) {
				case TcpPacket::kRead:
					return recv(port, message->source, packet->session, packet->read.nbyte);
				case TcpPacket::kWrite:
					return send(port, packet);
				case TcpPacket::kClose:
					return close(port, message->source, packet->id);
				case TcpPacket::kShutdown:
					return shutdown(port, message->source, packet->id);
				case TcpPacket::kOpts:
					return setopts(port, packet);
				case TcpPacket::kLowWaterMark:
					return lowWaterMark(port, packet->lowWaterMark.on, packet->lowWaterMark.value);
				case TcpPacket::kHighWaterMark:
					return highWaterMark(port, packet->highWaterMark.on, packet->highWaterMark.value);
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
					return handleIoWriteEvent(port, event->fd);
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
		// printf("port->id() = %d, setopts\n", port->id());

		if (packet->opts.optsbits & 0B00000001) {
			// printf("port->id() = %d, opts.reuseaddr = %d\n", port->id(), packet->opts.reuseaddr);
			reuseaddr_ = packet->opts.reuseaddr;
		}
		if (packet->opts.optsbits & 0B00000010) {
			// printf("port->id() = %d, opts.reuseport = %d\n", port->id(), packet->opts.reuseport);
			reuseport_ = packet->opts.reuseport;
		}
		if (packet->opts.optsbits & 0B00000100) {
			// printf("port->id() = %d, opts.keepalive = %d\n", port->id(), packet->opts.keepalive);
			keepalive_ = packet->opts.keepalive;
			sockets::setKeepAlive(fd_, keepalive_);
		}
		if (packet->opts.optsbits & 0B00001000) {
			// printf("port->id() = %d, opts.nodelay   = %d\n", port->id(), packet->opts.nodelay);
			nodelay_ = packet->opts.nodelay;
			sockets::setTcpNoDelay(fd_, nodelay_);
		}
		if (packet->opts.optsbits & 0B00010000) {
			// printf("port->id() = %d, opts.active    = %d\n", port->id(), packet->opts.active);
			active_ = packet->opts.active;
		}
		if (packet->opts.optsbits & 0B00100000) {
			// printf("port->id() = %d, opts.owner     = %d\n", port->id(), packet->opts.owner);
			if (port->owner() != packet->opts.owner) {
				PortCtl(port, port->id(), port->owner(), packet->opts.owner);
				port->setOwner(packet->opts.owner);
			}
		}
		if (packet->opts.optsbits & 0B01000000) {
			// printf("port->id() = %d, opts.read      = %d\n", port->id(), packet->opts.read);
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
		int savedErrno = 0;
		ssize_t n = inputBuffer_.readFd(fd, &savedErrno);
		if (n > 0) {
			readCount_ += n;
		}
		// printf("port->id() = %d, new message = %zd, active = %d, fd = %d, \n", port->id(),  n, active_, fd_);
		if (active_) {
			if (n > 0) {
				TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket) + n);
				packet->type = TcpPacket::kRead;
				packet->id = port->id();
				packet->session = 0;
				packet->read.nbyte = n;
				memcpy(packet->read.data, inputBuffer_.peek(), n);
				inputBuffer_.retrieve(n);
				auto msg  = port->makeMessage();
				msg->type = MSG_TYPE_TCP;
				msg->data = packet;
				msg->size = sizeof(TcpPacket) + n;
				port->send(port->owner(), msg);
				return true;
			} else if (n == 0) {
				if (state_ == kDisconnecting) {
					port->channel(fd_)->disableReading()->update();
					close(port, port->owner(), port->id());
				} else {
					TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
					packet->type = TcpPacket::kRead;
					packet->id = port->id();
					packet->session = 0;
					packet->read.nbyte = 0;
					auto msg  = port->makeMessage();
					msg->type = MSG_TYPE_TCP;
					msg->data = packet;
					msg->size = sizeof(TcpPacket);
					port->send(port->owner(), msg);

					port->channel(fd_)->disableReading()->update();
					state_ = kDisconnecting;
				}
				return true;
			} else {
				char t_errnobuf[512] = {'\0'};
				strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
				fprintf(stderr, "port->id() = %d, fd = %d read error=%s\n", port->id(), fd, t_errnobuf);
				return true;
			}
		} else {
			if (n > 0) {
				if (inputBuffer_.readableBytes() >= highWaterMark_) {
					port->channel(fd_)->disableReading()->update();
				}
			} else if (n == 0) {
				if (state_ == kDisconnecting) {
					port->channel(fd_)->disableReading()->update();
					close(port, port->owner(), port->id());
				} else {
					port->channel(fd_)->disableReading()->update();
					state_ = kDisconnecting;
				}
			}
		}

		return true;
	}
	bool handleIoWriteEvent(PortPtr port, int fd)
	{
		// static uint32_t i = 0;
		// i++;
		assert(fd_ == fd);

		if (port->channel(fd_)->isWriting()) {
			ssize_t n = sockets::write(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
			// if (i < 5) {
				// printf("port->id() = %d, total = %zd, write = %zd, i = %d\n", port->id(), outputBuffer_.readableBytes(), n, i);
			// }
			

			if (n > 0) {
				writeCount_ += n;
				outputBuffer_.retrieve(n);
				// 低水位回调
				if (outputBuffer_.readableBytes() <= lowWaterMark_ && lowWaterMarkResponse_ && state_ == kConnected) {
					TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
					packet->type = TcpPacket::kLowWaterMark;
					packet->id = port->id();
					packet->session = 0;
					packet->lowWaterMark.on = true;
					packet->lowWaterMark.value = outputBuffer_.readableBytes();
					auto msg     = port->makeMessage();
					msg->type    = MSG_TYPE_TCP;
					msg->data    = packet;
					msg->size    = sizeof(TcpPacket);
					port->send(port->owner(), msg);
				}
				if (outputBuffer_.readableBytes() == 0) {
					port->channel(fd_)->disableWriting()->update();
					if (state_ == kDisconnecting) {
						// close(port, port->owner(), port->id());
						sockets::shutdownWrite(fd_);
					}
				}
			} else {
				char t_errnobuf[512] = {'\0'};
				strerror_r(errno, t_errnobuf, sizeof t_errnobuf);
				// fprintf(stderr, "port->id() = %d, fd = %d, errno = %d, write error = %s\n", port->id(), fd, errno, t_errnobuf);
				port->channel(fd_)->disableWriting()->update();
				// if (state_ == kDisconnecting)
				// {
				//   sockets::shutdownWrite(connfd_);
				// }
			}
		} else {
			//LOG_TRACE << "Connection fd = " << channel_->fd() << " is down, no more writing";
		}
		return true;
	}

	bool send(PortPtr port, TcpPacket* packet)
	{
		// printf("port->id() = %d, send total message = %d\n", port->id(), cmd->towrite.nbyte);
		const void* data = static_cast<const void*>(packet->write.data);
		size_t len = packet->write.nbyte;
		ssize_t nwrote = 0;
		size_t remaining = len;
		bool faultError = false;
		if (state_ != kConnected) {
			return true;
		}
		
		// if no thing in output queue, try writing directly
		if (!port->channel(fd_)->isWriting() && outputBuffer_.readableBytes() == 0) {
			nwrote = sockets::write(fd_, data, len);
			if (nwrote >= 0) {
				writeCount_ += nwrote;
				remaining = len - nwrote;
				// 低水位回调
				if (remaining <= lowWaterMark_ && lowWaterMarkResponse_) {
					TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
					packet->type = TcpPacket::kLowWaterMark;
					packet->id = port->id();
					packet->session = 0;
					packet->lowWaterMark.on = true;
					packet->lowWaterMark.value = remaining;
					auto msg     = port->makeMessage();
					msg->type    = MSG_TYPE_TCP;
					msg->data    = packet;
					msg->size    = sizeof(TcpPacket);
					port->send(port->owner(), msg);
				}
			} else { // nwrote < 0
				nwrote = 0;
				if (errno != EWOULDBLOCK) {
					if (errno == EPIPE || errno == ECONNRESET) { // FIXME: any others?
						faultError = true;
					}
				}
			}
		}

		assert(remaining <= len);
		if (!faultError && remaining > 0) {
			size_t oldLen = outputBuffer_.readableBytes();
			// 高水位回调
			if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkResponse_) {
				TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
				packet->type = TcpPacket::kHighWaterMark;
				packet->id = port->id();
				packet->session = 0;
				packet->highWaterMark.on = true;
				packet->highWaterMark.value = oldLen + remaining;
				auto msg     = port->makeMessage();
				msg->type    = MSG_TYPE_TCP;
				msg->data    = packet;
				msg->size    = sizeof(TcpPacket);
				port->send(port->owner(), msg);
			}
			outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
			if (!port->channel(fd_)->isWriting()) {
				// printf("port->id() = %d, enableWriting\n", port->id());
				port->channel(fd_)->enableWriting()->update();
			}
		}
		// printf("port->id() = %d, send save  message = %zd\n", port->id(), outputBuffer_.readableBytes());
		return true;
	}
	bool recv(PortPtr port, uint32_t source, uint32_t session, int nbyte)
	{
		size_t size = static_cast<size_t>(nbyte);
		assert(port->owner() == source);
		assert(state_ == kConnected || state_ == kDisconnecting);
	
		// printf("port->id() = %d, total = %zu, read msg start size = %d\n", port->id(), inputBuffer_.readableBytes(), size);
		
		if (state_ == kDisconnecting && inputBuffer_.readableBytes() == 0) {
			// printf("port->id() = %d, read msg 0, close connection\n", port->id());
			
			TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
			packet->type = TcpPacket::kRead;
			packet->id = port->id();
			packet->session = session;
			packet->read.nbyte = 0;
			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_TCP;
			msg->data    = packet;
			msg->size    = sizeof(TcpPacket);
			port->send(source, msg);

			return true;
		}
		if (state_ == kDisconnecting && inputBuffer_.readableBytes() <= size) {
			// printf("port->id() = %d, read all msg, close connection\n", port->id());
			int n = inputBuffer_.readableBytes();
			TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket) + n);
			packet->type = TcpPacket::kRead;
			packet->id = port->id();
			packet->session = session;
			packet->read.nbyte = n;
			memcpy(packet->read.data, inputBuffer_.peek(), n);
			inputBuffer_.retrieve(n);
			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_TCP;
			msg->data    = packet;
			msg->size    = sizeof(TcpPacket) + n;

			port->send(source, msg);

			return true;
		}
		if (state_ == kConnected && read_ && inputBuffer_.readableBytes() <= size && !port->channel(fd_)->isReading()) {
			port->channel(fd_)->enableReading()->update();
			return false;
		}
		if (inputBuffer_.readableBytes() == 0) {
			return false;
		}
		if (inputBuffer_.readableBytes() < size) {
			return false;
		}
		int n = size>0? size : inputBuffer_.readableBytes();
		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket) + n);
		packet->type = TcpPacket::kRead;
		packet->id = port->id();
		packet->session = session;
		packet->read.nbyte = n;
		memcpy(packet->read.data, inputBuffer_.peek(), n);
		inputBuffer_.retrieve(n);
		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket) + n;
		port->send(source, msg);

		// printf("port->id() = %d, read msg size %d [ok]\n", port->id(), n);
		return true;
	}
	bool shutdown(PortPtr port, uint32_t source, uint32_t id)
	{
		// printf("port->id() = %d, shutdown\n", port->id());
		assert(port->owner() == source);
		assert(port->id() == id);
		if (state_ == kConnected || state_ == kDisconnecting) {
			
			// printf("shutdown to close1\n");
			if (state_ == kDisconnecting) {
				close(port, port->owner(), port->id());
				return true;
			}
			if (!port->channel(fd_)->isWriting()) {
				// printf("shutdown to close2\n");
				// close(port, port->owner(), port->id());
				sockets::shutdownWrite(fd_);
				state_ = kDisconnecting;
				return true;
			}
		}
		return true;
	}
	
	bool close(PortPtr port, uint32_t source, uint32_t id)
	{
		// printf("port->id() = %d, close\n", port->id());
		assert(port->owner() == source);
		assert(port->id() == id);
		assert(state_ == kConnected || state_ == kDisconnecting);
		PortCtl(port, port->id(), port->owner(), 0);
		// we don't close fd, leave it to dtor, so we can find leaks easily.
		port->exit();
		return true;
	}

	bool lowWaterMark(PortPtr port, bool on, uint64_t value)
	{
		lowWaterMarkResponse_ = on;
		lowWaterMark_ = value;
		return true;
	}
	bool highWaterMark(PortPtr port, bool on, uint64_t value)
	{
		highWaterMarkResponse_ = on;
		highWaterMark_ = value;
		return true;
	}
	bool getinfo(PortPtr port, uint32_t source, uint32_t session)
	{
		struct sockaddr_in6 addr6 = sockets::getPeerAddr(fd_);
		struct sockaddr* addr = (struct sockaddr*)&addr6;

		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		memset(packet, 0, sizeof(TcpPacket));
		packet->type = TcpPacket::kInfo;
		packet->id = port->id();
		packet->session = session;
		sockets::toIp(packet->info.ip, 64, addr);
		packet->info.port   = sockets::toPort(addr);
		packet->info.ipv6   = addr->sa_family == AF_INET6;

		memcpy(packet->info.start, start_.data(), start_.size());
		packet->info.owner  = port->owner();
		packet->info.readCount  = readCount_;
		packet->info.readBuff   = inputBuffer_.readableBytes();
		packet->info.writeCount = writeCount_;
		packet->info.writeBuff  = outputBuffer_.readableBytes();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);
		port->send(source, msg);

		return true;
	}
private:
	enum State {kConnecting, kConnected, kDisconnecting, kDisconnected};
	State state_;
	int fd_ = 0;
	Buffer inputBuffer_;
	Buffer outputBuffer_;

	// 高低水位回调设置
	bool lowWaterMarkResponse_  = false;
	bool highWaterMarkResponse_ = false;
	uint64_t lowWaterMark_      = 0;
	uint64_t highWaterMark_     = 1024*1024*1024;
	
	bool reuseaddr_ = false;
	bool reuseport_ = false;
	bool keepalive_ = false;
	bool nodelay_   = false;
	bool active_    = false;
	bool read_      = false;

	std::string start_;
	uint32_t readCount_  = 0;
	uint32_t writeCount_ = 0;
};


reg(TcpConnnection)

