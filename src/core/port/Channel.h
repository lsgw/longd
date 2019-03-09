#ifndef CHANNEL_H
#define CHANNEL_H
#include <functional>
#include <memory>

class Port;
class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

class Channel : public std::enable_shared_from_this<Channel> {
	using EventCallback = std::function<void(void)>;

public:
	Channel(uint32_t id, int fd, Port* port = NULL);

	uint32_t id() const { return id_; }
	int      fd() const { return fd_; }

	int event() const { return events_; }
	int revent() const { return revents_; }
	
	void set_eventCallback(const EventCallback& cb) { eventCallback_ = cb; }
	void set_event(int e) { events_ = e; }
	void set_revent(int e) { revents_ = e; }
	
	ChannelPtr enableReading();
	ChannelPtr enableWriting();
	ChannelPtr disableReading();
	ChannelPtr disableWriting();
	ChannelPtr disableAll();

	void update();

	bool isReading() const { return events_ & kReadEvent; }
	bool isWriting() const { return events_ & kWriteEvent; }
	bool isNoneEvent() const { return events_ == kNoneEvent; }

	bool hasReadEvent() const { return revents_ & kReadEvent; }
	bool hasWriteEvent() const { return revents_ & kWriteEvent; }
	bool hasErrorEvent() const { return revents_ & kErrorEvent; }

	void handleEvent();

private:
	uint32_t id_;      // port id
	int      fd_;      // 文件描述符
	int      events_;  // 注册的事件
	int      revents_; // 发生的事件
	Port*    port_;    // 宿主port

	EventCallback eventCallback_;
public:                                 
	static const int kNoneEvent  = 0B000; // 无任何事件
	static const int kReadEvent  = 0B001; // 可读事件
	static const int kWriteEvent = 0B010; // 可写事件
	static const int kErrorEvent = 0B100; // 错误事件
};

#endif