#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "noncopyable.h"
#include "Poller.h"
#include "Port.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <map>

class Channel;

class EventLoop : noncopyable {
	
public:
	EventLoop();
	~EventLoop();

	void updateChannel(Channel* ch);
	
	void checkIo(bool wait);


	uint32_t newPort(const std::string& driver, uint32_t handle, uint32_t type, void* data, uint32_t size);

	bool unregisterPort(uint32_t id);
	uint32_t registerPort(PortPtr port);
	PortPtr grab(uint32_t id);

	void push(PortPtr port);
	PortPtr pop();

	bool dispatchPort();
	bool postCommand(uint32_t id, MessagePtr message);
	
	void wakeup();
	void wakeupRead();

	int wakeupfd() const;
	std::map<uint32_t, uint32_t> portList();
	std::string name(uint32_t id);
private:
	std::unique_ptr<Poller> poller_;
	std::vector<Channel*> activeChannels_;

	RWLock   lock_;
	uint32_t index_;
	uint32_t size_;
	std::vector<PortPtr> slot_;

	SpinLock spin_;
	SpinLock spinPending_;

	std::list<PortPtr> ports_;
	std::list<PortPtr> portsPending_;

	std::shared_ptr<Channel> wakeupChannel_;
	int wakeupFd_[2];

	static const int ms = 3600000;
};


#endif