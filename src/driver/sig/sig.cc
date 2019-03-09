#include "driver.h"
#include "sockets.h"
#include "sig.h"
#include <sys/socket.h>
#include "utils.h"
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <map>
#include <set>

#ifdef __MACH__
#define SIGCHLD 20
#else
#define SIGCHLD 17
#endif

static int sigFd_[2] = {0, 0};

static void handler(int sig)
{
	ssize_t n = sockets::write(sigFd_[1], &sig, sizeof(sig));
	if (n != sizeof(sig)) {
		fprintf(stderr, "sig handler writes %zd bytes\n", n);
	} else {
    	// fprintf(stdout, "收到了信号 %s\n", strsignal(sig));
	}
}


class sig : public driver<sig> {
public:
	void init(PortPtr port, MessagePtr& message) override
	{
		signal(SIGPIPE, SIG_IGN);
		if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd_) < 0) {
			fprintf(stderr, "Failed in socketpair\n");
			assert(false);
		}
		struct sigaction sa;
		sigfillset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = handler;
		if (sigaction(SIGCHLD, &sa, NULL) == -1) {
			fprintf(stderr, "取消信号处理器出错\n");
		} else {
			// fprintf(stderr, "取消信号处理器成功\n");
		}
		port->channel(sigFd_[0])->enableReading()->update();
		PortCtl(port, port->id(), 0, port->owner());
		// fprintf(stderr, "sig id=%d init ok\n", port->id());
	}
	void release(PortPtr port, MessagePtr& message) override
	{
		// fprintf(stderr, "sig id=%d release ok\n", port->id());
	}
	bool receive(PortPtr port, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_SIG) {
			SigPacket* packet = static_cast<SigPacket*>(message->data);
			switch (packet->type) {
				case SigPacket::kActionModule:
					return moduleRegister(port, packet->actionModule.handle, packet->actionModule.sig);
				case SigPacket::kActionDriver:
					return driverRegister(port, packet->actionDriver.id, packet->actionDriver.sig);
				case SigPacket::kCancelModule:
					return moduleUnRegister(port, packet->cancelModule.handle, packet->cancelModule.sig);
				case SigPacket::kCancelDriver:
					return driverUnRegister(port, packet->cancelDriver.id, packet->cancelDriver.sig);

				case SigPacket::kActionCHLD:
					return processRegister(port, packet->actionCHLD.id, packet->actionCHLD.pid);
				case SigPacket::kCancelCHLD:
					return processUnRegister(port, packet->cancelCHLD.id, packet->cancelCHLD.pid);
				case SigPacket::kInfo:
					return getinfo(port, message->source);
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
		return true;
	}

	bool handleIoReadEvent(PortPtr port, int fd)
	{
		// fprintf(stderr, "handleIoReadEvent\n");
		assert(fd == sigFd_[0]);
		int sig = 0;
		int n = sockets::read(fd, &sig, sizeof(sig));
		assert(n == sizeof(sig));
		if (sig == SIGCHLD) {
			int status;
			int savedErrno;
			pid_t pid;
			std::set<pid_t> child;

			savedErrno = errno;         /* In case we modify 'errno' */
			while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
				// printf("Reaped child %ld\n", (long) pid);
				child.insert(pid);
			}
			// printf("child size = %d\n", child.size());
			// assert(pid == -1 && errno != ECHILD);
			errno = savedErrno;

			for (pid_t pid : child) {
				if (ChldPidToDriver_.find(pid) != ChldPidToDriver_.end()) {
					SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
					packet->type = SigPacket::kHappenCHLD;
					packet->happenCHLD.pid = pid;
					auto msg     = port->makeMessage();
					msg->type    = MSG_TYPE_SIG;
					msg->data    = packet;
					msg->size    = sizeof(SigPacket);
					// printf("command child exit id[%d], pid[%d]\n", ChldPidToDriver_[pid], pid);
					port->command(ChldPidToDriver_[pid], msg);
				}
			}

		}
		{
			SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
			packet->type = SigPacket::kHappen;
			packet->happen.sig = sig;

			auto msg     = port->makeMessage();
			msg->type    = MSG_TYPE_SIG;
			msg->data    = packet;
			msg->size    = sizeof(SigPacket);

			for (uint32_t handle : sigToModule_[sig]) {
				port->send(handle, msg);
			}
			for (uint32_t id : sigToDriver_[sig]) {
				port->command(id, msg);
			}
		}
		return true;
	}

	bool moduleRegister(PortPtr port, uint32_t handle, int sig)
	{
		if (sig != SIGCHLD && sigToModule_[sig].size() == 0 && sigToDriver_[sig].size() == 0) {
			// fprintf(stderr, "registerSig:%d\n", sig);
			struct sigaction sa;
			sigfillset(&sa.sa_mask);
			sa.sa_flags = 0;
			sa.sa_handler = handler;
			if (sigaction(sig, &sa, NULL) == -1) {
				fprintf(stderr, "安装信号处理器出错\n");
			} else {
				// fprintf(stderr, "安装信号处理器成功\n");
			}
		}
		sigToModule_[sig].insert(handle);

		return true;
	}
	bool driverRegister(PortPtr port, uint32_t id, int sig)
	{
		if (sig != SIGCHLD && sigToModule_[sig].size() == 0 && sigToDriver_[sig].size() == 0) {
			// fprintf(stderr, "registerSig:%d\n", sig);
			struct sigaction sa;
			sigfillset(&sa.sa_mask);
			sa.sa_flags = 0;
			sa.sa_handler = handler;
			if (sigaction(sig, &sa, NULL) == -1) {
				fprintf(stderr, "安装信号处理器出错\n");
			} else {
				// fprintf(stderr, "安装信号处理器成功\n");
			}
		}
		sigToDriver_[sig].insert(id);

		return true;
	}

	bool moduleUnRegister(PortPtr port, uint32_t handle, int sig)
	{
		sigToModule_[sig].erase(handle);
		if (sig != SIGCHLD && sigToModule_[sig].size() == 0 && sigToDriver_[sig].size() == 0) {
			struct sigaction sa;
			sigfillset(&sa.sa_mask);
			sa.sa_flags = 0;
			sa.sa_handler = SIG_DFL;
			if (sigaction(sig, &sa, NULL) == -1) {
				fprintf(stderr, "取消信号处理器出错\n");
			} else {
				// fprintf(stderr, "取消信号处理器成功\n");
			}
		}
		return true;
	}
	bool driverUnRegister(PortPtr port, uint32_t id, int sig)
	{
		sigToDriver_[sig].erase(id);
		if (sig != SIGCHLD && sigToModule_[sig].size() == 0 && sigToDriver_[sig].size() == 0) {
			struct sigaction sa;
			sigfillset(&sa.sa_mask);
			sa.sa_flags = 0;
			sa.sa_handler = SIG_DFL;
			if (sigaction(sig, &sa, NULL) == -1) {
				fprintf(stderr, "取消信号处理器出错\n");
			} else {
				// fprintf(stderr, "取消信号处理器成功\n");
			}
		}
		return true;
	}
	bool processRegister(PortPtr port, uint32_t id, pid_t pid)
	{
		// printf("processRegister id[%d], pid[%d]\n", id, pid);
		ChldPidToDriver_.insert({pid, id});
		return true;
	}
	bool processUnRegister(PortPtr port, uint32_t id, pid_t pid)
	{
		// printf("processUnRegister id[%d], pid[%d]\n", id, pid);
		ChldPidToDriver_.erase(pid);
		(void)id;
		return true;
	}
	bool getinfo(PortPtr port, uint32_t source)
	{
		std::string siglist;
		siglist += "{";
		int i = 0;
		for (auto m : sigToModule_) {
			int sig = m.first;
			auto ss = m.second;
			if (i != 0) {
				siglist += ",";
			}
			siglist += "\"";
			siglist += std::to_string(sig);
			siglist += "\"";
			siglist += ":[";
			int j = 0;
			for (auto s : ss) {
				if (j != 0) {
					siglist += ",";
				}
				siglist += std::to_string(s);
				j++;
			}
			siglist += "]";
			i++;
		}
		siglist += "}";

		SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket) + siglist.size());
		packet->type = SigPacket::kInfo;
		memcpy(packet->info.data, siglist.data(), siglist.size());
		packet->info.nbyte = siglist.size();

		auto msg     = port->makeMessage();
		msg->type    = MSG_TYPE_SIG;
		msg->data    = packet;
		msg->size    = sizeof(SigPacket) + siglist.size();
		port->send(source, msg);

		return true;
	}
private:
	std::map<int, std::set<uint32_t>> sigToModule_;
	std::map<int, std::set<uint32_t>> sigToDriver_;
	std::map<pid_t, uint32_t> ChldPidToDriver_;
};

reg(sig)
