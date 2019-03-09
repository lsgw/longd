#ifndef CONTEXT_H
#define CONTEXT_H

#include "noncopyable.h"
#include "MutexLock.h"
#include "SpinLock.h"
#include "Module.h"
#include "Monitor.h"
#include "Env.h"
#include <memory>
#include <list>
#include <map>
#include <functional>

class Context : public noncopyable, public std::enable_shared_from_this<Context> {
public:
	Context(void* actor);
	~Context();

	uint32_t  handle() const { return handle_; }
	ModulePtr module() const { return module_; }
	void*     entry() const { return entry_; }

	uint32_t newsession();
	uint32_t newservice(const std::string& module, uint32_t type, void* data, uint32_t size);
	uint32_t newport(const std::string& driver, uint32_t type, void* data, uint32_t size);

	void setHandle(uint32_t  handle) { handle_ = handle; }
	void setModule(ModulePtr module) { module_ = module; }

	void recv(MessagePtr message, int priority);
	bool send(uint32_t handle, MessagePtr message, int priority = 0);
	bool command(uint32_t id, MessagePtr message);
	
	void dispatch(Monitor::WatcherPtr watcher);

	void timeout(double second, uint32_t session);
	uint32_t mailboxLength();

	Env& env();
	
	void exit();
	void yield();
	void abort();
	
	MessagePtr makeMessage() const;

	MessagePtr get(uint32_t mid);
	uint32_t save(MessagePtr);
	bool free(uint32_t mid);

	uint64_t cpuCost() const;
	uint32_t messageCount();
	void setProfile(bool on);
	bool getProfile() const;

	std::map<uint32_t, uint32_t> portList();
	std::string portname(uint32_t id);
private:
	uint32_t  handle_;
	ModulePtr module_;
	uint32_t  session_;
	void*     entry_;

	SpinLock              spinPending_;
	std::list<MessagePtr> mailboxPending_;
	bool                  global_;

	std::list<MessagePtr> mailbox_;
	std::list<MessagePtr> mailboxHold_;

	uint32_t                saveMessageIndex_;
	uint32_t                saveMessageSize_;
	std::vector<MessagePtr> saveMessageList_;
	
	uint64_t cpuCost_;
	uint32_t messageCount_;
	bool     profile_;
};

#endif