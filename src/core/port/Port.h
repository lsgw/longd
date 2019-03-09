#ifndef PORT_H
#define PORT_H
#include "noncopyable.h"
#include "Interface.h"
#include "SpinLock.h"
#include "MutexLock.h"
#include "Channel.h"
#include "Env.h"
#include <string>
#include <memory>
#include <vector>
#include <list>
#include <map>
class Port : public noncopyable, public std::enable_shared_from_this<Port> {
public:
	Port(void* entry);
	~Port();

	uint32_t id() const {  return id_; }
	uint32_t owner() const { uint32_t o = 0; {SpinLockGuard lock(ownerlock_); o = owner_; } return o; }
	InterfacePtr interface() const { return interface_; }

	void setId(uint32_t id) { id_ = id; }
	void setOwner(uint32_t handle) { SpinLockGuard lock(ownerlock_); owner_ = handle; }
	void setInterface(InterfacePtr interface) { interface_ = interface; }
	
	uint32_t newport(const std::string& driver, uint32_t type, void* data, uint32_t size);
	ChannelPtr channel(int fd);
	void registerEvent(ChannelPtr ch);
	
	void dispatch();
	
	void recv(MessagePtr message);
	bool send(uint32_t handle, MessagePtr message);
	bool command(uint32_t id, MessagePtr message);

	Env& env();
	void exit();
	
	MessagePtr makeMessage() const;
	std::string name() const;
private:
	void triggerEvent();
	

	InterfacePtr interface_;
	void*        entry_;
	uint32_t     id_;

	mutable SpinLock ownerlock_;
	uint32_t         owner_;

	mutable SpinLock      spin_;
	std::list<MessagePtr> mailboxPending_;
	std::list<MessagePtr> mailbox_;
	std::list<MessagePtr> mailboxHold_;
	bool                  global_;

	// 用于注册事件和保存发生的事件
	std::map<int, ChannelPtr> channels_;
};




#endif