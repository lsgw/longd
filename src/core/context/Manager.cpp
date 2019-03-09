#include "Manager.h"
#include "Module.h"
#include "EventLoop.h"
#include "Singleton.h"

#define DEFAULT_SLOT_SIZE 8
#define HANDLE_MASK 0xffffffff

Manager::Manager() :
	loop(true),
	lock_(),
	index_(1),
	size_(DEFAULT_SLOT_SIZE),
	slot_(DEFAULT_SLOT_SIZE, ContextPtr())
{

}
uint32_t Manager::newContext(const std::string& module, uint32_t handle, uint32_t type, void* data, uint32_t size)
{
	ModulePtr m = Singleton<ModuleList>::instance().query(module);

	if (!m) {
		fprintf(stderr, "new %s fail\n", module.c_str());
		return 0;
	}

	void* actor = m->create();
	if (!actor) {
		fprintf(stderr, "create %s fail\n", module.c_str());
		return 0;
	}

	ContextPtr ctx(new Context(actor));
	ctx->setHandle(registerContext(ctx));
	ctx->setModule(m);

	auto msg     = ctx->makeMessage();
	msg->source  = handle;
	msg->type    = type;
	msg->data    = data;
	msg->size    = size;
	
	ctx->recv(msg, 0);

	return ctx->handle();
}


bool Manager::unregisterContext(uint32_t handle)
{
	bool ret = false;

	lock_.wrlock();

	uint32_t hash = handle & (size_ - 1);
	ContextPtr ctx = slot_[hash];

	if (ctx && ctx->handle() == handle) {
		slot_[hash] = std::shared_ptr<Context>();
		ret = true;
	}
	
	lock_.unlock();

	return ret;
}

uint32_t Manager::registerContext(ContextPtr ctx)
{
	lock_.wrlock();

	while (true) {
		for(uint32_t i=0; i<size_; i++) {
			uint32_t handle = (i + index_) & HANDLE_MASK;
			uint32_t hash = handle & (size_ - 1);

			if (!slot_[hash] && handle>0) {
				slot_[hash] = ctx;
				index_ = handle + 1;
				lock_.unlock();
				return handle;
			}
		}
		assert((size_*2 - 1) < HANDLE_MASK);
		
		std::vector<ContextPtr> new_slot(size_*2, ContextPtr());
		for (uint32_t i=0; i<size_; i++) {
			uint32_t hash = slot_[i]->handle() & (size_*2-1);
			assert(slot_[i].get() != NULL);
			new_slot[hash] = slot_[i];
		}

		slot_.swap(new_slot);
		size_ *= 2;
	}

	return 0;
}

ContextPtr Manager::grab(uint32_t handle)
{
	ContextPtr result;
	
	lock_.rdlock();

	uint32_t hash = handle & (size_ - 1);
	//printf("hash = %d, size-1=%d,", hash, size_-1);
	if (slot_[hash] && slot_[hash]->handle() == handle) {
		result = slot_[hash];
	}
	
	lock_.unlock();


	return result;
}


void Manager::push(ContextPtr ctx)
{
	SpinLockGuard lockPending(spinPending_);
	contextsPending_.push_front(ctx);
}

ContextPtr Manager::pop()
{
	ContextPtr ctx;
	{
		SpinLockGuard lock(spin_);
		if (contexts_.empty()) {
			{
				SpinLockGuard lockPending(spinPending_);
				if (!contextsPending_.empty()) {
					contexts_.swap(contextsPending_);
				}
			}
			if (!contexts_.empty()) {
				ctx = contexts_.back();
				contexts_.pop_back();
			}
		} else {
			ctx = contexts_.back();
			contexts_.pop_back();
		}
	}
	return ctx;
}

bool Manager::dispatchContext(Monitor::WatcherPtr watcher)
{
	auto ctx = pop();
	
	if (ctx) {
		ctx->dispatch(watcher);
		return false;
	} else {
		return true;
	}
}

bool Manager::postMessage(uint32_t handle, MessagePtr message, int priority)
{
	ContextPtr ctx = grab(handle);
	// printf("postMessage handle:%u, source:%u, session:%d", handle, message->source, message->session);
	if (ctx) {
		// printf(" find\n");
		ctx->recv(message, priority);
		return true;
	} else {
		// printf(" not find\n");
		return false;
	}
}

void Manager::abort()
{
	loop = false;
	Singleton<Monitor>::instance().notsleep.notifyAll();
	Singleton<EventLoop>::instance().wakeup();
}
