#include "EventLoop.h"
#include "Singleton.h"
#include "sockets.h"
#include "Channel.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>


#define DEFAULT_SLOT_SIZE 4
#define ID_MASK 0xffffffff

EventLoop::EventLoop() :
	poller_(new Poller()),
	activeChannels_(),
	lock_(),
	index_(1),
	size_(DEFAULT_SLOT_SIZE),
	slot_(DEFAULT_SLOT_SIZE, PortPtr()),
	spin_(),
	spinPending_(),
	ports_(),
	portsPending_()
{
	fprintf(stderr, "EventLoop create\n");
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, wakeupFd_) < 0) {
		fprintf(stderr,"Failed in socketpair\n");
		abort();
	}
	wakeupChannel_.reset(new Channel(0, wakeupFd_[0], NULL));
	wakeupChannel_->set_eventCallback(std::bind(&EventLoop::wakeupRead, this));
	wakeupChannel_->enableReading();
	updateChannel(wakeupChannel_.get());
}
EventLoop::~EventLoop()
{
	fprintf(stderr, "EventLoop destry\n");
	wakeupChannel_->disableAll();
	updateChannel(wakeupChannel_.get());
	::close(wakeupFd_[0]);
	::close(wakeupFd_[1]);
}

int checkIoNum = 0;
void EventLoop::checkIo(bool wait)
{
	//fprintf(stderr, "checkIo start checkIoNum = %d\n", checkIoNum++);
	activeChannels_.clear();
	poller_->poll(&activeChannels_, wait? ms : 0);
	for (auto ch : activeChannels_) {
		ch->handleEvent();
	}
}

void EventLoop::updateChannel(Channel* ch)
{
	poller_->updateChannel(ch);
}

uint32_t EventLoop::newPort(const std::string& driver, uint32_t handle, uint32_t type, void* data, uint32_t size)
{
	InterfacePtr m = Singleton<InterfaceList>::instance().query(driver);
	if (!m) {
		fprintf(stderr, "new %s fail\n", driver.c_str());
		return 0;
	}
	void* entry = m->create();
	if (!entry) {
		fprintf(stderr, "create %s fail\n", driver.c_str());
		return 0;
	}

	PortPtr port(new Port(entry));
	port->setId(registerPort(port));
	port->setInterface(m);
	port->setOwner(handle);

	auto msg     = port->makeMessage();
	msg->source  = handle;
	msg->type    = type;
	msg->data    = data;
	msg->size    = size;
	
	port->recv(msg);

	return port->id();
}


bool EventLoop::unregisterPort(uint32_t id)
{
	bool ret = false;

	lock_.wrlock();

	uint32_t hash = id & (size_ - 1);
	PortPtr port = slot_[hash];

	if (port && port->id() == id) {
		slot_[hash] = std::shared_ptr<Port>();
		ret = true;
	}

	lock_.unlock();

	return ret;
}

uint32_t EventLoop::registerPort(PortPtr port)
{
	lock_.wrlock();

	while (true) {
		for(uint32_t i=0; i<size_; i++) {
			uint32_t id = (i + index_) & ID_MASK;
			uint32_t hash = id & (size_ - 1);
			// printf("cmp --> index = %d, id = %d, hash = %d, size = %d\n", index_, id, hash, size_);
			if (!slot_[hash] && id>0) {
				slot_[hash] = port;
				index_ = id + 1;
				lock_.unlock();
				// printf("end --> index = %d, id = %d, hash = %d, size = %d\n", index_, id, hash, size_);
				return id;
			}
		}
		assert((size_*2 - 1) < ID_MASK);
		// printf("new slot\n");		
		std::vector<PortPtr> new_slot(size_*2, PortPtr());
		for (uint32_t i=0; i<size_; i++) {
			uint32_t hash = slot_[i]->id() & (size_*2-1);
			assert(slot_[i].get() != NULL);
			new_slot[hash] = slot_[i];
		}

		slot_.swap(new_slot);
		size_ *= 2;
	}

	return 0;
}

PortPtr EventLoop::grab(uint32_t id)
{
	PortPtr result;
	
	lock_.rdlock();

	uint32_t hash = id & (size_ - 1);
	if (slot_[hash] && slot_[hash]->id() == id) {
		result = slot_[hash];
	}
	
	lock_.unlock();


	return result;
}


void EventLoop::push(PortPtr port)
{
	{
		SpinLockGuard lockPending(spinPending_);
		portsPending_.push_front(port);
		//printf("EventLoop push global port id = %u\n", port->id());
	}
	wakeup();
}

PortPtr EventLoop::pop()
{
	PortPtr port;
	{
		SpinLockGuard lock(spin_);
		if (ports_.empty()) {
			{
				SpinLockGuard lockPending(spinPending_);
				if (!portsPending_.empty()) {
					ports_.swap(portsPending_);
				}
			}
			if (!ports_.empty()) {
				port = ports_.back();
				ports_.pop_back();
			}
		} else {
			port = ports_.back();
			ports_.pop_back();
		}
	}
	return port;
}

bool EventLoop::dispatchPort()
{
	PortPtr port = pop();
	
	if (port) {
		port->dispatch();
		return true;
	} else {
		return false;
	}
}

bool EventLoop::postCommand(uint32_t id, MessagePtr message)
{
	PortPtr port = grab(id);
	if (port) {
		port->recv(message);
		return true;
	} else {
		return false;
	}
}


void EventLoop::wakeup()
{
	uint64_t one = 1;
	ssize_t n = sockets::write(wakeupFd_[1], &one, sizeof one);
	if (n != sizeof one) {
		fprintf(stderr, "EventLoop::wakeup() writes %zd bytes instead of 8\n", n);
	}
}
void EventLoop::wakeupRead()
{
	uint64_t one = 1;
	ssize_t n = sockets::read(wakeupFd_[0], &one, sizeof one);
	if (n != sizeof one) {
		fprintf(stderr, "EventLoop::wakeupRead() reads %zd bytes instead of 8\n", n);
	}
}

std::map<uint32_t, uint32_t> EventLoop::portList()
{
	std::map<uint32_t, uint32_t> mlist;
	
	lock_.rdlock();
	for (uint32_t i=0; i<size_; i++) {
		if (slot_[i]) {
			mlist.insert({slot_[i]->id(), slot_[i]->owner()});
		}
	}
	lock_.unlock();

	return mlist;
}

std::string EventLoop::name(uint32_t id)
{
	PortPtr port = grab(id);
	if (port) {
		return port->name();
	} else {
		return std::string("");
	}
}

