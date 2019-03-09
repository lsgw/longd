#include "Port.h"
#include "Singleton.h"
#include "EventLoop.h"
#include "Manager.h"
#include <unistd.h>

Port::Port(void* entry) :
	interface_(),
	entry_(entry),
	id_(0),
	ownerlock_(),
	owner_(0),
	spin_(),
	mailboxPending_(),
	mailbox_(),
	mailboxHold_(),
	global_(false),
	channels_()
{
	//fprintf(stderr, "new driver\n");
}
Port::~Port()
{
	//fprintf(stderr, "del driver\n");
}

void Port::dispatch()
{	
	std::list<MessagePtr> mailboxSave;
	std::list<ChannelPtr> activechannels;
	for (auto& e : channels_) {
		 auto& ch = e.second;
		if (ch->hasReadEvent() || ch->hasWriteEvent() || ch->hasErrorEvent()) {
			activechannels.push_front(ch);
		}
	}
	if (!activechannels.empty()) {
		for (auto ch : activechannels) {
			if (ch->hasReadEvent()) {
				Event* event = (Event*)malloc(sizeof(Event));
				event->type = Event::kRead;
				event->fd   = ch->fd();

				auto msg = makeMessage();
				msg->type    = MSG_TYPE_EVENT;
				msg->data    = event;
				msg->size    = sizeof(Event);
				
				interface_->callback(shared_from_this(), entry_, msg);
			}
			if (ch->hasWriteEvent()) {
				Event* event = (Event*)malloc(sizeof(Event));
				event->type = Event::kWrite;
				event->fd   = ch->fd();

				auto msg = makeMessage();
				msg->type    = MSG_TYPE_EVENT;
				msg->data    = event;
				msg->size    = sizeof(Event);

				interface_->callback(shared_from_this(), entry_, msg);
			}
			if (ch->hasErrorEvent()) {
				Event* event = (Event*)malloc(sizeof(Event));
				event->type = Event::kError;
				event->fd   = ch->fd();

				auto msg = makeMessage();
				msg->type    = MSG_TYPE_EVENT;
				msg->data    = event;
				msg->size    = sizeof(Event);

				interface_->callback(shared_from_this(), entry_, msg);
			}
			ch->set_revent(Channel::kNoneEvent);
		}
		if (!mailboxHold_.empty()) {
			mailboxSave.swap(mailboxHold_);
			while (!mailboxSave.empty()){
				MessagePtr message = mailboxSave.back();
				mailboxSave.pop_back();
				bool match = interface_->callback(shared_from_this(), entry_, message);
				if (!match) {
					mailboxHold_.push_front(message);
				}
			}
		}
	}
	
	if (mailbox_.empty()) {
		SpinLockGuard lock(spin_);
		if (!mailboxPending_.empty()) {
			mailbox_.swap(mailboxPending_);
		}
	}
	
	for (int i=0; i<2 && !mailbox_.empty(); i++) {
		MessagePtr message = mailbox_.back();
		mailbox_.pop_back();
		if (message->type == MSG_TYPE_EXIT) {
			interface_->release(shared_from_this(), entry_, message);
			Singleton<EventLoop>::instance().unregisterPort(id_);
			return ;
		}

		bool match = interface_->callback(shared_from_this(), entry_, message);
		if (!match) {
			mailboxHold_.push_front(message);
		}
	}
	{
		SpinLockGuard lock(spin_);
		if (mailbox_.size() + mailboxPending_.size() > 0) {
			global_ = true;
			Singleton<EventLoop>::instance().push(shared_from_this());
		} else {
			global_ = false;
		}
	}
}

uint32_t Port::newport(const std::string& driver, uint32_t type, void* data, uint32_t size)
{
	return Singleton<EventLoop>::instance().newPort(driver, owner_, type, data, size);
}

ChannelPtr Port::channel(int fd)
{
	ChannelPtr ch(new Channel(id_, fd, this));
	auto it = channels_.find(fd);
	if (it != channels_.end()) {
		ch->set_event(it->second->event());
	}
	return ch;
}

void Port::registerEvent(ChannelPtr ch)
{
	int  fd = ch->fd();
	auto it = channels_.find(fd);
	if ( it == channels_.end()) {
		if (!ch->isNoneEvent()) {
			ch->set_eventCallback(std::bind(&Port::triggerEvent, this));
			channels_[fd] = ch;
			Singleton<EventLoop>::instance().updateChannel(channels_[fd].get());
		}
	} else {
		if (!ch->isNoneEvent()) {
			channels_[fd]->set_event(ch->event());
			Singleton<EventLoop>::instance().updateChannel(channels_[fd].get());
		} else {
			channels_[fd]->set_event(ch->event());
			Singleton<EventLoop>::instance().updateChannel(channels_[fd].get());
			channels_.erase(fd);
		}
	}
}

void Port::triggerEvent()
{
	SpinLockGuard lock(spin_);
	if (!global_) {
		global_ = true;
		Singleton<EventLoop>::instance().push(shared_from_this());
	}	
}

void Port::recv(MessagePtr message)
{
	SpinLockGuard lock(spin_);
	mailboxPending_.push_front(message);
	if (!global_) {
		global_ = true;
		Singleton<EventLoop>::instance().push(shared_from_this());
	}
}
bool Port::send(uint32_t handle, MessagePtr message)
{
	bool succ = Singleton<Manager>::instance().postMessage(handle, message, 0);
	if (!succ) {
		this->exit();
	}
	return succ;
}
bool Port::command(uint32_t id, MessagePtr message)
{
	return Singleton<EventLoop>::instance().postCommand(id, message);
}

Env& Port::env()
{
	return Singleton<Env>::instance();
}

void Port::exit()
{
	auto msg = std::make_shared<Message>();
	msg->source = id_;
	msg->type   = MSG_TYPE_EXIT;
	recv(msg);
}

MessagePtr Port::makeMessage() const
{
	auto msg = std::make_shared<Message>();
	msg->source = id_;
	return msg;
}

std::string Port::name() const
{
	return interface_->name();
}
