#include "driver.h"
#include "sockets.h"
#include "utils.h"
#include "Buffer.h"
#include "exe.h"
#include "sig.h"
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <string>
#include <vector>

#define SIGKILL 9

bool fileIsExist(const std::string& path)
{
	if (access(path.c_str(), X_OK) == 0) {
		return true;
	} else {
		return false;
	}
}
int closeAllFd()
{
	struct dirent *entry, _entry;
	int retval, rewind, fd;
	DIR* dir = opendir("/dev/fd");
	if (dir == NULL) {
		return -1;
	}

	rewind = 0;
	while (1) {
		retval = readdir_r(dir, &_entry, &entry);
		if (retval != 0) {
			errno = -retval;
			retval = -1;
			break;
		}
		if (entry == NULL) {
			if (!rewind) {
				break;
			}
			rewinddir(dir);
			rewind = 0;
			continue;
		}
		if (entry->d_name[0] == '.') {
			continue;
		}
		fd = atoi(entry->d_name);
		if (dirfd(dir) == fd || fd == 0 || fd == 1 || fd == 2) {
			continue;
		}

		retval = close(fd);
		if (retval != 0) {
			break;
		}
		rewind = 1;
	}

	closedir(dir);

	return retval;
}

class exe : public driver<exe> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		state_ = kOpening;
		assert(message->type == MSG_TYPE_EXE);
		ExePacket* packet = static_cast<ExePacket*>(message->data);
		assert(packet->type == ExePacket::kOpen);
		uint32_t session = packet->session;

		// std::string pathname(packet->open.path, packet->open.len);
		// bool exist = fileIsExist(pathname);
		// printf("xxxx exe path: %s, %d\n", pathname.c_str(), exist);

		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
		// gmtime_r(&now, &tm);
		strftime(timebuf, sizeof timebuf, "%Y.%m.%d-%H:%M:%S", &tm);
		start_ = timebuf;

		cmdline_ = std::string(packet->open.path, packet->open.len);
		std::vector<std::string> ss = utils::split(cmdline_, " ");
		std::string pathname = ss[0];
		bool exist = fileIsExist(pathname);

		int comfd[2] = { 0 };
		if (exist && ::socketpair(AF_UNIX, SOCK_STREAM, 0, comfd) < 0) {
			fprintf(stderr,"Failed in socketpair\n");
			
			packet = (ExePacket*)malloc(sizeof(ExePacket));
			bzero(packet, sizeof(ExePacket));
			packet->type = ExePacket::kStatus;
			packet->id = port->id();
			packet->session = session;
			packet->status.pid = 0;
			packet->status.online = false;

			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_EXE;
			msg->data    = packet;
			msg->size    = sizeof(ExePacket);
			port->send(message->source, msg);
			port->exit();
			fd_ = -1;
			return;
		} else {
			fd_ = comfd[0];
		}
		pid_t pid = fork();
		switch (pid) {
		case -1: {
				packet = (ExePacket*)malloc(sizeof(ExePacket));
				bzero(packet, sizeof(ExePacket));
				packet->type = ExePacket::kStatus;
				packet->id = port->id();
				packet->session = session;
				packet->status.pid = 0;
				packet->status.online = false;

				auto msg     = port->makeMessage();
				msg->type    = MSG_TYPE_EXE;
				msg->data    = packet;
				msg->size    = sizeof(ExePacket);
				port->send(message->source, msg);
				port->exit();
			}
			return;
		case  0: {
				
				if (dup2(comfd[1], STDIN_FILENO) < 0) {
					exit(0);
				}
				if (dup2(comfd[1], STDOUT_FILENO) < 0) {
					exit(0);
				}
				if (dup2(comfd[1], STDERR_FILENO) < 0) {
					exit(0);
				}
				::close(comfd[0]);
				closeAllFd();
				
				char** argv = (char**)malloc(sizeof(char**) * (ss.size() + 1));
				memset(argv, 0, sizeof(char**) * (ss.size() + 1));
				for (std::vector<std::string>::size_type i=0; i<ss.size(); i++) {
					char* data = (char*)malloc(ss[i].size()+1);
					memset(data, 0, ss[i].size()+1);
					memcpy(data, ss[i].data(), ss[i].size());
					argv[i] = data;
				}
				// fprintf(stderr, "我是子进程, 我的PID为: %d\n", getpid());
				execve(pathname.c_str(), argv, NULL);
				fprintf(stderr, "运行到这里， 肯定有问题\n");
				exit(0);
			}
			return;
		default: {
				::close(comfd[1]);
				// printf("我是父进程, 我的PID为: %d\n", getpid());
				
				ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
				bzero(packet, sizeof(ExePacket));
				packet->type = ExePacket::kStatus;
				packet->id = port->id();
				packet->session = session;
				packet->status.pid = pid;
				packet->status.online = true;

				auto msg     = port->makeMessage();
				msg->type    = MSG_TYPE_EXE;
				msg->data    = packet;
				msg->size    = sizeof(ExePacket);
				port->send(message->source, msg);
				PortCtl(port, port->id(), 0, port->owner());

			} {
				SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
				packet->type = SigPacket::kActionCHLD;
				packet->actionCHLD.id = port->id();
				packet->actionCHLD.pid = pid;

				auto msg     = port->makeMessage();
				msg->type    = MSG_TYPE_SIG;
				msg->data    = packet;
				msg->size    = sizeof(SigPacket);
				port->command(port->env().sigid, msg);

				pid_ = pid;
				state_ = kOpened;
				// printf("exe id=%d init pid = %d\n", port->id(), pid_);
			}
			return;
		}
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		if (fd_ >= 0) { 
			port->channel(fd_)->disableAll()->update();
			::close(fd_);
		}
		if (pid_ > 0) {
			SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
			packet->type = SigPacket::kCancelCHLD;
			packet->cancelCHLD.id = port->id();
			packet->cancelCHLD.pid = pid_;

			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_SIG;
			msg->data    = packet;
			msg->size    = sizeof(SigPacket);
			port->command(port->env().sigid, msg);
		}
		if (pid_>0 && !receiveExitSig) {
			kill(pid_, SIGKILL);
		}
		// printf("exe id=%d release pid = %d\n", port->id(), pid_);
	}

	bool receive(PortPtr port, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_EXE) {
			ExePacket* packet = static_cast<ExePacket*>(message->data);
			switch (packet->type) {
				case ExePacket::kWrite:
					return write(port, packet);
				case ExePacket::kRead:
					return read(port, message->source, packet->session, packet->read.nbyte);
				case ExePacket::kClose:
					return close(port, message->source, packet->id);
				case ExePacket::kOpts:
					return setopts(port, packet);
				case ExePacket::kInfo:
					return getinfo(port, message->source, packet->session);
				default:
					return true;
			}
		} else if (message->type == MSG_TYPE_SIG) {
			SigPacket* packet = static_cast<SigPacket*>(message->data);
			assert(packet->type == SigPacket::kHappenCHLD);
			switch (packet->type) {
				case SigPacket::kHappenCHLD:
					return processExit(port, packet->happenCHLD.pid);
				default:
					assert(false);
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
	bool setopts(PortPtr port, ExePacket* packet)
	{
		// fprintf(stderr, "exe id=%d setopts\n", port->id());

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
		int savedErrno = 0;
		ssize_t n = inputBuffer_.readFd(fd, &savedErrno);
		if (n > 0) {
			readCount_ += n;
		}
		// printf("port->id() = %d, new message = %zd, active = %d, fd = %d, \n", port->id(),  n, active_, fd_);
		if (active_) {
			if (n > 0) {
				ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + n);
				packet->type = ExePacket::kRead;
				packet->id = port->id();
				packet->session = 0;
				packet->read.nbyte = n;
				memcpy(packet->read.data, inputBuffer_.peek(), n);
				inputBuffer_.retrieve(n);
				auto msg  = port->makeMessage();
				msg->type = MSG_TYPE_EXE;
				msg->data = packet;
				msg->size = sizeof(ExePacket) + n;
				port->send(port->owner(), msg);
				return true;
			} else if (n == 0) {
				ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
				packet->type = ExePacket::kRead;
				packet->id = port->id();
				packet->session = 0;
				packet->read.nbyte = 0;
				auto msg  = port->makeMessage();
				msg->type = MSG_TYPE_EXE;
				msg->data = packet;
				msg->size = sizeof(ExePacket);
				port->send(port->owner(), msg);

				port->channel(fd_)->disableReading()->update();
				
				return true;
			} else {
				char t_errnobuf[512] = {'\0'};
				strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
				fprintf(stderr, "port->id() = %d, fd = %d read error=%s\n", port->id(), fd, t_errnobuf);
				return true;
			}
		} else {
			if (n == 0) {
				port->channel(fd_)->disableReading()->update();
				state_ = kClosing;
			}
		}

		return true;
	}
	bool processExit(PortPtr port, pid_t pid)
	{
		// printf("processExit pid[%d]\n", pid);
		assert(pid_ == pid);
		// port->channel(fd_)->disableReading()->update();
		// state_ = kClosing;
		receiveExitSig = true;
		return true;
	}

	bool read(PortPtr port, uint32_t source, uint32_t session, int nbyte)
	{
		size_t size = static_cast<size_t>(nbyte);
		assert(port->owner() == source);
		assert(state_ == kOpened || state_ == kClosing);
	
		// printf("port->id() = %d, total = %zu, read msg start size = %d\n", port->id(), inputBuffer_.readableBytes(), size);
		
		if (state_ == kClosing && inputBuffer_.readableBytes() == 0) {
			// printf("port->id() = %d, read msg 0, close connection\n", port->id());
			
			ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
			packet->type = ExePacket::kRead;
			packet->id = port->id();
			packet->session = session;
			packet->read.nbyte = 0;
			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_EXE;
			msg->data    = packet;
			msg->size    = sizeof(ExePacket);
			port->send(source, msg);

			return true;
		}
		if (state_ == kClosing && inputBuffer_.readableBytes() <= size) {
			// printf("port->id() = %d, read all msg, close connection\n", port->id());
			int n = inputBuffer_.readableBytes();
			ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + n);
			packet->type = ExePacket::kRead;
			packet->id = port->id();
			packet->session = session;
			packet->read.nbyte = n;
			memcpy(packet->read.data, inputBuffer_.peek(), n);
			inputBuffer_.retrieve(n);
			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_EXE;
			msg->data    = packet;
			msg->size    = sizeof(ExePacket) + n;

			port->send(source, msg);

			return true;
		}
		if (state_ == kOpened && read_ && inputBuffer_.readableBytes() <= size && !port->channel(fd_)->isReading()) {
			port->channel(fd_)->enableReading()->update();
			return false;
		}
		if (inputBuffer_.readableBytes() == 0) {
			return false;
		}
		if (inputBuffer_.readableBytes() < size) {
			return false;
		}
		
		int n = static_cast<int>(size>0? size : inputBuffer_.readableBytes());
		ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + n);
		packet->type = ExePacket::kRead;
		packet->id = port->id();
		packet->session = session;
		packet->read.nbyte = n;
		memcpy(packet->read.data, inputBuffer_.peek(), n);
		inputBuffer_.retrieve(n);
		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_EXE;
		msg->data    = packet;
		msg->size    = sizeof(ExePacket) + n;
		port->send(source, msg);

		// printf("port->id() = %d, read msg size %d [ok]\n", port->id(), n);
		return true;
	}
	bool write(PortPtr port, ExePacket* packet)
	{
		// printf("port->id() = %d, send total message = %d\n", port->id(), cmd->towrite.nbyte);
		const void* data = static_cast<const void*>(packet->write.data);
		size_t len = packet->write.nbyte;
		ssize_t nwrote = 0;
		size_t remaining = len;
		bool faultError = false;
		if (state_ != kOpened) {
			return true;
		}
		
		// if no thing in output queue, try writing directly
		if (!port->channel(fd_)->isWriting() && outputBuffer_.readableBytes() == 0) {
			nwrote = sockets::write(fd_, data, len);
			// printf("port->id() = %d, write n bytes = %zd\n", port->id(), nwrote);
			if (nwrote >= 0) {
				writeCount_ += nwrote;
				remaining = len - nwrote;
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
			// size_t oldLen = outputBuffer_.readableBytes();
			outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
			if (!port->channel(fd_)->isWriting()) {
				// printf("port->id() = %d, enableWriting\n", port->id());
				port->channel(fd_)->enableWriting()->update();
			}
		}
		// printf("port->id() = %d, send save  message = %zd\n", port->id(), outputBuffer_.readableBytes());
		return true;
	}
	bool handleIoWriteEvent(PortPtr port, int fd)
	{
		assert(fd_ == fd);

		if (port->channel(fd_)->isWriting()) {
			ssize_t n = sockets::write(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
			if (n > 0) {
				writeCount_ += n;
				outputBuffer_.retrieve(n);
				if (outputBuffer_.readableBytes() == 0) {
					port->channel(fd_)->disableWriting()->update();
					if (state_ == kClosing) {
						// close(port, port->owner(), port->id());
						// sockets::shutdownWrite(fd_);
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

	bool close(PortPtr port, uint32_t source, uint32_t id)
	{
		// printf("port->id() = %d, close\n", port->id());
		assert(port->owner() == source);
		assert(port->id() == id);
		assert(state_ == kOpened || state_ == kClosing);
		PortCtl(port, port->id(), port->owner(), 0);
		// we don't close fd, leave it to dtor, so we can find leaks easily.
		port->exit();
		return true;
	}
	bool getinfo(PortPtr port, uint32_t source, uint32_t session)
	{
		int n = cmdline_.size();
		ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + n);
		memset(packet, 0, sizeof(ExePacket));
		packet->type = ExePacket::kInfo;
		packet->id = port->id();
		packet->session = session;
		memcpy(packet->info.data, cmdline_.data(), n);
		packet->info.nbyte = n;

		memcpy(packet->info.start, start_.data(), start_.size());
		packet->info.owner  = port->owner();
		packet->info.readCount  = readCount_;
		packet->info.readBuff   = inputBuffer_.readableBytes();
		packet->info.writeCount = writeCount_;
		packet->info.writeBuff  = outputBuffer_.readableBytes();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_EXE;
		msg->data    = packet;
		msg->size    = sizeof(ExePacket) + n;
		port->send(source, msg);

		return true;
	}
private:
	std::string cmdline_;
	enum State {kOpening, kOpened, kClosing, kClosed};
	State state_;
	Buffer inputBuffer_;
	Buffer outputBuffer_;
	bool receiveExitSig = false;
	int fd_;
	pid_t pid_ = 0;
	bool read_ = false;
	bool active_ = false;

	std::string start_;
	uint32_t readCount_  = 0;
	uint32_t writeCount_ = 0;
};

reg(exe)