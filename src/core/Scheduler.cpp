#include "Scheduler.h"
#include "Singleton.h"
#include "EventLoop.h"
#include "Monitor.h"
#include "Manager.h"
#include "Timer.h"
#include <string.h>
#include <assert.h>
#include <errno.h>

void Scheduler::start(const Env& env)
{
	// 每个线程都有一个权重 权重为-1只处理一条消息 权重为0就将此服务的所有消息处理完 权重大于1就处理服务的部分消息
	static int weight[] = { 
		-1, -1, -1, -1,  0,  0,  0,  0,
		 1,  1,  1,  1,  1,  1,  1,  1, 
		 2,  2,  2,  2,  2,  2,  2,  2, 
		 3,  3,  3,  3,  3,  3,  3,  3,
	};
	Singleton<InterfaceList>::instance().setInterfacePath(env.driverPath);
	Singleton<ModuleList>::instance().setModulePath(env.modulePath);
	Singleton<EventLoop>::instance();
	Singleton<Manager>::instance();
	Singleton<Monitor>::instance();
	Singleton<Timer>::instance();
	Singleton<Env>::instance() = env;

	Singleton<Monitor>::instance().setWorkThreadNum(env.workerThread);
	for (int i=0; i<static_cast<int>(Singleton<Monitor>::instance().watchers.size()); i++) {
		if (i < static_cast<int>(sizeof(weight)/sizeof(weight[0]))) {
			Singleton<Monitor>::instance().watchers[i]->weight = weight[i];
		} else {
			Singleton<Monitor>::instance().watchers[i]->weight = 0;
		}
	}
	Singleton<Timer>::instance().setTimerCallback([](uint32_t handle, uint32_t session) {
		auto msg     = std::make_shared<Message>();
		// printf("Scheduler timer handle=%d, session=%d\n", handle, session);
		uint32_t* data = (uint32_t*)malloc(sizeof(uint32_t));
		*data = session;
		msg->source = handle;
		msg->type   = MSG_TYPE_TIME;
		msg->data   = data;
		msg->size   = sizeof(uint32_t);
		Singleton<Manager>::instance().postMessage(handle, msg, 0);
	});

	for (int i=0; i<static_cast<int>(3+env.workerThread); i++) {
		if (i == 0) {
			threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Scheduler::threadEventLoop, this))));
		} else if (i == 1) {
			threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Scheduler::threadMonitor, this))));
		} else if (i == 2) {
			threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Scheduler::threadTimer, this))));
		} else {
			threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Scheduler::threadWork, this, i-3))));
		}
	}
	for (auto& t : threads_) {
		t->start();
	}
	
	char* data = (char*)malloc(env.args.size());
	memset(data, 0, env.args.size());
	memcpy(data, env.args.data(), env.args.size());
	
	Singleton<Manager>::instance().newContext(env.start, 0, MSG_TYPE_JSON, data, env.args.size());

	// /* Wait for a signal to arrive... */
	// while () {
	// 	int res = select(0, NULL, NULL, NULL, NULL);
	// 	assert(res < 0);
	// 	assert(errno == EINTR);
	// 	// printf("signal arrive\n");
	// }

	for (int i=threads_.size()-1; i>=0; i--) {
		threads_[i]->join();
	}
}

void Scheduler::threadMonitor()
{
	while (Singleton<Manager>::instance().loop) {
		for (int i=0; i<static_cast<int>(Singleton<Monitor>::instance().watchers.size()); i++) {
			bool b = Singleton<Monitor>::instance().watchers[i]->check();
			if (!b) {
				int   size = sizeof(char) * 128;
				char* data = (char*)malloc(size);
				snprintf(data, size, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", Singleton<Monitor>::instance().watchers[i]->source(), Singleton<Monitor>::instance().watchers[i]->destination(), Singleton<Monitor>::instance().watchers[i]->version());
				
				auto msg  = std::make_shared<Message>();
				msg->type = MSG_TYPE_LOG;
				msg->data = data;
				msg->size = size;
				Singleton<Manager>::instance().postMessage(1, msg, 0);
			}
		}
		for (int i=0; i<5; i++) {
			sleep(1);
		}
	}
}


#define noWait false
#define doWait true
void Scheduler::threadEventLoop()
{
	while (Singleton<Manager>::instance().loop) {
		while (Singleton<EventLoop>::instance().dispatchPort()) {
			   Singleton<EventLoop>::instance().checkIo(noWait);
		}
		Singleton<EventLoop>::instance().checkIo(doWait);
	}
}

void Scheduler::threadTimer()
{
	while (Singleton<Manager>::instance().loop) {
		Singleton<Timer>::instance().updatetime();
		if (Singleton<Monitor>::instance().sleepThread() > 0) {
			Singleton<Monitor>::instance().notsleep.notify();
		}
		usleep(2500);
	}
}
void Scheduler::threadWork(int threadIndex)
{
	bool isSleep = false;
	Singleton<Monitor>::instance().watchers[threadIndex]->tid = CurrentThread::tid();

	while (Singleton<Manager>::instance().loop) {
		isSleep = Singleton<Manager>::instance().dispatchContext(Singleton<Monitor>::instance().watchers[threadIndex]);
		if (isSleep) {
			MutexLockGuard lock(Singleton<Monitor>::instance().mutex);
			Singleton<Monitor>::instance().sleepThreadInc();
			Singleton<Monitor>::instance().notsleep.wait();
			Singleton<Monitor>::instance().sleepThreadDec();
		}
	}
}
