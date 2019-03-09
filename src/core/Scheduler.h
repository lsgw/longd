#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "noncopyable.h"
#include "Thread.h"
#include "Env.h"
#include <vector>
#include <memory>

class Scheduler : noncopyable {
public:
	void start(const Env& env);
private:
	void threadEventLoop();
	void threadMonitor();
	void threadTimer();
	void threadWork(int threadIndex);

	std::vector<std::unique_ptr<Thread>> threads_;
};

#endif