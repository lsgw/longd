#include "Singleton.h"
#include "EventLoop.h"
#include "Context.h"
#include "Manager.h"
#include "Timer.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>


std::string maiboxToString(std::list<MessagePtr> s1, std::list<MessagePtr> s2, std::list<MessagePtr> s3)
{
	std::string s;
	for (auto m : s1) {
		s += "(";
		s += std::to_string(m->type);
		s += ",";
		s += std::to_string(m->source);
		s +=") ";
	}
	for (auto m : s2) {
		s += "(";
		s += std::to_string(m->type);
		s += ",";
		s += std::to_string(m->source);
		s +=") ";
	}
	for (auto m : s3) {
		s += "(";
		s += std::to_string(m->type);
		s += ",";
		s += std::to_string(m->source);
		s +=") ";
	}
	return s;
}

uint64_t current_thread_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
	return cp;
}

#define DEFAULT_SAVE_MESSAGE_SIZE 4
#define DEFAULT_SAVE_MESSAGE_MASK 0xff

Context::Context(void* entry) :
	handle_(0),
	module_(),
	session_(0),
	entry_(entry),
	spinPending_(),
	mailboxPending_(),
	global_(false),
	mailbox_(),
	mailboxHold_(),
	saveMessageIndex_(1),
	saveMessageSize_(DEFAULT_SAVE_MESSAGE_SIZE),
	saveMessageList_(DEFAULT_SAVE_MESSAGE_SIZE, MessagePtr()),
	cpuCost_(0),
	messageCount_(0),
	profile_(false)
{
	profile_ = Singleton<Env>::instance().profile;
	//fprintf(stderr, "new Context\n");
}
Context::~Context()
{
	//fprintf(stderr, "del Context\n");
}

uint32_t Context::newsession()
{
	  ++session_;
	if (session_ == 0) {
		session_ = 1;
	}
	return session_;
}

void Context::dispatch(Monitor::WatcherPtr watcher)
{
	// printf("[:%08x] -start- mailbox mailboxLength = %u, s = %s\n", handle_, mailboxLength(), maiboxToString(mailboxPending_, mailbox_, mailboxHold_).c_str());
	int n = 1;
	MessagePtr message;
	for (int i=0; i<n; i++) {
		
		if (mailbox_.empty()) {
			SpinLockGuard lock(spinPending_);
			mailbox_.swap(mailboxPending_);
		}
		message = mailbox_.back();
		mailbox_.pop_back();

		if (i==0 && watcher->weight>=0) {
			{
				SpinLockGuard lock(spinPending_);
				n = mailboxPending_.size();
			}
			n += mailbox_.size();
			n >>= watcher->weight;
		}
		
		if (message->type == MSG_TYPE_EXIT) {
			module_->release(shared_from_this(), entry_, message);
			Singleton<Manager>::instance().unregisterContext(handle_);
			return ;
		}
		bool match = false;
		if (profile_) {
			uint64_t cpuStart = current_thread_time();
			watcher->trigger(message->source, handle_);
			match = module_->callback(shared_from_this(), entry_, message);
			watcher->trigger(0, 0);
			uint64_t cpuEnd   = current_thread_time();
			cpuCost_ += cpuEnd - cpuStart;
		} else {
			watcher->trigger(message->source, handle_);
			match = module_->callback(shared_from_this(), entry_, message);
			watcher->trigger(0, 0);
		}
		if (!match) {
			mailboxHold_.push_front(message);
		} else if (mailboxHold_.size() > 0 && (mailbox_.empty() || (mailbox_.back()->type != 0 && mailbox_.back()->type != MSG_TYPE_EXIT && mailbox_.back()->type != MSG_TYPE_DEBUG))) {
			while (!mailboxHold_.empty()) {
				MessagePtr hold = mailboxHold_.front();
				mailboxHold_.pop_front();
				mailbox_.push_back(hold);
			}
		}
	}
	{
		SpinLockGuard lock(spinPending_);
		if (mailbox_.size() + mailboxPending_.size() > 0) {
			global_ = true;
			Singleton<Manager>::instance().push(shared_from_this());
		} else {
			global_ = false;
		}
	}
	// printf("[:%08x] -end- mailbox mailboxLength = %u, s = %s\n", handle_, mailboxLength(), maiboxToString(mailboxPending_, mailbox_, mailboxHold_).c_str());
}

uint32_t Context::newservice(const std::string& module, uint32_t type, void* data, uint32_t size)
{
	return Singleton<Manager>::instance().newContext(module, handle_, type, data, size);
}
uint32_t Context::newport(const std::string& driver, uint32_t type, void* data, uint32_t size)
{
	return Singleton<EventLoop>::instance().newPort(driver, handle_, type, data, size);
}

void Context::recv(MessagePtr message, int priority)
{
	if (priority == 0) {
		SpinLockGuard lock(spinPending_);
		mailboxPending_.push_front(message);
		if (!global_) {
			global_ = true;
			Singleton<Manager>::instance().push(shared_from_this());
		}
		messageCount_++;
		//printf("[:%08x] send msg priority=0, type=%u, source=%u, data=%p, size=%u\n", handle_, message->type, message->source, message->data, message->size);
	
	} else {
		SpinLockGuard lock(spinPending_);
		mailboxPending_.push_back(message);
		if (!global_) {
			global_ = true;
			Singleton<Manager>::instance().push(shared_from_this());
		}
		messageCount_++;
		//printf("[:%08x] send msg priority=0, type=%u, source=%u, data=%p, size=%u\n", handle_, message->type, message->source, message->data, message->size);
	
	}
}
bool Context::send(uint32_t handle, MessagePtr message, int priority)
{
	return Singleton<Manager>::instance().postMessage(handle, message, priority);
}
bool Context::command(uint32_t id, MessagePtr message)
{
	return Singleton<EventLoop>::instance().postCommand(id, message);
}
void Context::timeout(double second, uint32_t session)
{
	Singleton<Timer>::instance().timeout(handle_, second*100, session);
}

uint32_t Context::mailboxLength()
{
	uint32_t n = mailboxHold_.size() + mailbox_.size();
	
	{
		SpinLockGuard lock(spinPending_);
		n += mailboxPending_.size();
	}

	return n;
}
Env& Context::env()
{
	return Singleton<Env>::instance();
}

void Context::exit()
{
	auto msg = std::make_shared<Message>();
	msg->source = handle_;
	msg->type   = MSG_TYPE_EXIT;
	recv(msg, 0);
}
void Context::yield()
{
	MessagePtr msg;
	{
		SpinLockGuard lock(spinPending_);
		if (!mailboxPending_.empty()) {
			auto tmp = mailboxPending_.back();
			if (tmp->type == MSG_TYPE_EXIT || tmp->type == MSG_TYPE_DEBUG) {
				msg = tmp;
				mailboxPending_.pop_back();
			}
		}
	}
	mailbox_.push_back(makeMessage());
	if (msg) {
		mailbox_.push_back(msg);
	}
}

void Context::abort()
{
	Singleton<Manager>::instance().abort();
}

MessagePtr Context::makeMessage() const
{
	auto msg = std::make_shared<Message>();
	msg->source = handle_;
	return msg;
}


uint32_t Context::save(MessagePtr message)
{
	while (true) {
		for(uint32_t i=0; i<saveMessageSize_; i++) {
			uint32_t mid = (i + saveMessageIndex_) & DEFAULT_SAVE_MESSAGE_MASK;
			uint32_t hash = mid & (saveMessageSize_ - 1);

			if (!saveMessageList_[hash] && mid>0) {
				message->mid = mid;
				saveMessageList_[hash] = message;
				saveMessageIndex_ = mid + 1;
				return mid;
			}
		}
		assert((saveMessageSize_*2 - 1) < DEFAULT_SAVE_MESSAGE_MASK);
		
		std::vector<MessagePtr> new_save(saveMessageSize_*2, MessagePtr());
		for (uint32_t i=0; i<saveMessageSize_; i++) {
			uint32_t hash = saveMessageList_[i]->mid & (saveMessageSize_*2-1);
			assert(saveMessageList_[i].get() != NULL);
			assert(new_save[hash].get() == NULL);
			new_save[hash] = saveMessageList_[i];
		}

		saveMessageList_.swap(new_save);
		saveMessageSize_ *= 2;
	}

	return 0;

}
MessagePtr Context::get(uint32_t mid)
{
	MessagePtr result;
	
	uint32_t hash = mid & (saveMessageSize_ - 1);
	if (saveMessageList_[hash] && saveMessageList_[hash]->mid == mid) {
		result = saveMessageList_[hash];
	}

	return result;
}
bool Context::free(uint32_t mid)
{
	bool ret = false;

	uint32_t hash = mid & (saveMessageSize_ - 1);
	if (saveMessageList_[hash] && saveMessageList_[hash]->mid == mid) {
		saveMessageList_[hash] = MessagePtr();
		ret = true;
	}

	return ret;
}




uint64_t Context::cpuCost() const
{
	return cpuCost_;
}
uint32_t Context::messageCount()
{
	uint32_t count = 0;
	{
		SpinLockGuard lock(spinPending_);
		count = messageCount_;
	}
	return count;
}
void Context::setProfile(bool on)
{
	profile_ = on;
}
bool Context::getProfile() const
{
	return profile_;
}

std::map<uint32_t, uint32_t> Context::portList()
{
	return Singleton<EventLoop>::instance().portList();
}
std::string Context::portname(uint32_t id)
{
	return Singleton<EventLoop>::instance().name(id);
}
