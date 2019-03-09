#ifndef MANAGER_H
#define MANAGER_H

#include "noncopyable.h"
#include "MutexLock.h"
#include "SpinLock.h"
#include "RWLock.h"
#include "Context.h"
#include "Monitor.h"
#include <string>
#include <vector>
#include <list>

class Manager : public noncopyable {
public:
	Manager();
	uint32_t newContext(const std::string& module, uint32_t handle, uint32_t type, void* data, uint32_t size);

	bool unregisterContext(uint32_t handle);
	uint32_t registerContext(ContextPtr ctx);
	ContextPtr grab(uint32_t handle);

	void push(ContextPtr ctx);
	ContextPtr pop();

	bool dispatchContext(Monitor::WatcherPtr watcher);
	bool postMessage(uint32_t handle, MessagePtr message, int priority);

	void abort();
	bool loop;
private:
	
	RWLock   lock_;
	uint32_t index_;
	uint32_t size_;
	std::vector<ContextPtr> slot_;


	std::list<ContextPtr> contexts_;
	std::list<ContextPtr> contextsPending_;
	//MutexLock mutex_;
	SpinLock spin_;
	SpinLock spinPending_;
};


#endif