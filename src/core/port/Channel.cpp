#include "Channel.h"
#include "EventLoop.h"
#include "Port.h"
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

Channel::Channel(uint32_t id, int fd, Port* port) :
	id_(id),
	fd_(fd),
	events_(kNoneEvent),
	revents_(kNoneEvent),
	port_(port)
{

}
ChannelPtr Channel::enableReading()
{
	events_ |= kReadEvent;  // 设置可读事件
	return shared_from_this();
}
ChannelPtr Channel::enableWriting()
{
	events_ |= kWriteEvent; // 设置可写事件
	return shared_from_this();
}
ChannelPtr Channel::disableReading()
{
	events_ &= ~kReadEvent; // 取消可读事件注册
	return shared_from_this();
}
ChannelPtr Channel::disableWriting()
{
	events_ &= ~kWriteEvent;// 取消可写事件注册
	return shared_from_this();
}
ChannelPtr Channel::disableAll()
{
	events_ = kNoneEvent;   // 取消所有事件注册
	return shared_from_this();
}
void Channel::update()
{
	port_->registerEvent(shared_from_this());
}

void Channel::handleEvent()
{
	eventCallback_();
}