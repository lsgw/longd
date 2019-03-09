#ifndef MONITOR_H
#define MONITOR_H

#include "noncopyable.h"
#include "Condition.h"
#include <stdint.h>
#include <unistd.h>
#include <vector>
#include <memory>



class Monitor : noncopyable {
public:
	class Watcher : noncopyable {
	public:
		Watcher();
		void trigger(uint32_t source, uint32_t destination);
		bool check();
		
		uint32_t source();
		uint32_t destination();

		uint32_t version();
		
		int32_t  weight;
		pid_t    tid;

	private:
		uint32_t  source_;
		uint32_t  destination_;

		uint32_t  version_;
		uint32_t  checkVersion_;
	};
	using WatcherPtr = std::shared_ptr<Watcher>;
	
	Monitor();
	void setWorkThreadNum(int numThreads);
	void sleepThreadInc();
	void sleepThreadDec();
	int32_t sleepThread();
	
	std::vector<WatcherPtr> watchers;
	MutexLock mutex;
	Condition notsleep;
private:
	int32_t  sleepThread_;
};



#endif