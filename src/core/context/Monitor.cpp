#include "Monitor.h"
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)
#define ATOM_DEC(ptr) __sync_add_and_fetch(ptr, -1)
#define ATOM_GET(ptr) __sync_val_compare_and_swap(ptr, 0, 0)
#define ATOM_SET(ptr,val) __sync_lock_test_and_set(ptr, val)

Monitor::Watcher::Watcher() :
	weight(0),
	tid(0),
	source_(0),
	destination_(0),
	version_(0),
	checkVersion_(0)
{

}

void Monitor::Watcher::trigger(uint32_t source, uint32_t destination)
{
	ATOM_SET(&source_, source);
	ATOM_SET(&destination_, destination);
	ATOM_INC(&version_);	//原子自增
}

bool Monitor::Watcher::check()
{
	if ((ATOM_GET(&version_) == ATOM_GET(&checkVersion_)) && ATOM_GET(&destination_)) {
		return false;
	} else {
		ATOM_SET(&checkVersion_, version_);
		return true;
	}
}
uint32_t Monitor::Watcher::source()
{
	return ATOM_GET(&source_);
}

uint32_t Monitor::Watcher::destination()
{
	return ATOM_GET(&destination_);
}
uint32_t Monitor::Watcher::version()
{
	return ATOM_GET(&version_);
}



Monitor::Monitor() :
	mutex(),
	notsleep(mutex),
	sleepThread_(0)
{

}

void Monitor::setWorkThreadNum(int numThreads)
{
	watchers.clear();
	for (int i=0; i<numThreads; i++) {
		watchers.push_back(std::shared_ptr<Watcher>(new Watcher));
	}
}
void Monitor::sleepThreadInc()
{
	ATOM_INC(&sleepThread_);	//原子自增
}
void Monitor::sleepThreadDec()
{
	ATOM_DEC(&sleepThread_);	//原子自减
}
int32_t Monitor::sleepThread()
{
	return ATOM_GET(&sleepThread_);
}

