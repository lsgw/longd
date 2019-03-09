#include "driver.h"
#include "utils.h"
#include "sockets.h"
#include "tcp.h"

class TcpClient : public driver<TcpClient> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_TCP);
		TcpPacket* packet = static_cast<TcpPacket*>(message->data);
		assert(packet->type == TcpPacket::kConnect);
		session_ = packet->session;

		// printf("ip   = %s\n", packet->connect.ip);
		// printf("port = %d\n", packet->connect.port);
		// printf("ipv6 = %d\n", packet->connect.ipv6);

		bzero(&addr6, sizeof(addr6));
		bzero(&addr4, sizeof(addr4));

		if (packet->connect.ipv6) {
			addr = sockets::fromIpPort(packet->connect.ip, packet->connect.port, &addr6);
		} else {
			addr = sockets::fromIpPort(packet->connect.ip, packet->connect.port, &addr4);
		}
		if (addr == NULL) {
			connectFail(port);
		} else {
			connect(port);
		}
		// fprintf(stderr, "TcpClient id=%d init\n", port->id());
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		// fprintf(stderr, "TcpClient id=%d release\n", port->id());
	}

	bool receive(PortPtr port, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_EVENT);
		Event* event = static_cast<Event*>(message->data);
		switch (event->type) {
			case Event::kRead:
				return true;
			case Event::kWrite:
				return handleIoWriteEvent(port, event->fd);
			case Event::kError:
				return true;
			default:
				return true;
		}
	}

	bool handleIoWriteEvent(PortPtr port, int fd)
	{
		// printf("connecting handleIoWriteEvent\n");
		assert(fd_ == fd);
		port->channel(fd)->disableWriting()->update();

		int err = sockets::getSocketError(fd);
		if (err) {
			// printf("errno = %d, %s\n", err, strerror(err));
			sockets::close(fd);
			retry(port);
			return true;
		} else if (sockets::isSelfConnect(fd)) {
			// printf("connecting isSelfConnect\n");
			sockets::close(fd);
			retry(port);
			return true;
		}


		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		bzero(packet, sizeof(TcpPacket));
		packet->type = TcpPacket::kShift;
		packet->id = 0;
		packet->session = 0;
		packet->shift.fd = fd;
		uint32_t connid = port->newport("TcpConnection", MSG_TYPE_TCP, packet, sizeof(TcpPacket));
		assert(connid > 0);


		packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		packet->type = TcpPacket::kStatus;
		packet->id = port->id();
		packet->session = session_;
		packet->status.online = true;
		packet->status.id = connid;

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);

		port->send(port->owner(), msg);
		port->exit();
		return true;
	}

	void connect(PortPtr port)
	{
		int fd = sockets::createTcpNonblockingOrDie(addr->sa_family);
		int ok = sockets::connect(fd, addr);

		int savedErrno = (ok == 0) ? 0 : errno;
		// printf("ok = %d, errno = %d, %s\n", ok, errno, strerror(errno));
		switch (savedErrno) {
		case 0:
		case EINPROGRESS:
		case EINTR:
		case EISCONN:
			// printf("connecting ... \n");
			connecting(port, fd);
			break;
		case EAGAIN:
		case EADDRINUSE:
		case EADDRNOTAVAIL:
		case ECONNREFUSED:
		case ENETUNREACH:
			// printf("retry ... \n");
			sockets::close(fd);
			retry(port);
			break;
		case EACCES:
		case EPERM:
		case EAFNOSUPPORT:
		case EALREADY:
		case EBADF:
		case EFAULT:
		case ENOTSOCK:
		default:
			// printf("connectFail ... \n");
			sockets::close(fd);
			connectFail(port);
			break;
		}
	}
	void connecting(PortPtr port, int fd)
	{
		fd_ = fd;
		port->channel(fd)->enableWriting()->update();
	}
	void retry(PortPtr port)
	{
		if (retryCount_ < retryTotal_) {
			connect(port);
			retryCount_++;
		} else {
			connectFail(port);
		}
	}

	void connectFail(PortPtr port)
	{
		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
		bzero(packet, sizeof(TcpPacket));
		packet->type = TcpPacket::kStatus;
		packet->id = port->id();
		packet->session = session_;
		packet->status.online = false;
		packet->status.id = 0;

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_TCP;
		msg->data    = packet;
		msg->size    = sizeof(TcpPacket);

		port->send(port->owner(), msg);
		port->exit();
	}

private:
	int fd_ = 0;
	int retryCount_ = 0;
	int retryTotal_ = 5;
	uint32_t session_ = 0;

	struct sockaddr_in6 addr6;
	struct sockaddr_in  addr4;
	struct sockaddr* addr = NULL;
};

reg(TcpClient)