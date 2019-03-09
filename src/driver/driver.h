#ifndef DRIVER_H
#define DRIVER_H

#include "noncopyable.h"
#include "Types.h"
#include "Port.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <functional>

void PortCtl(PortPtr port, uint32_t id, uint32_t oldOwner, uint32_t newOwner)
{
	char* data = (char*)malloc(128);
	snprintf(data, 128, "[\"cast\",\"ref\",%u,\"portctl\",%u,%u,%u]", id, id, oldOwner, newOwner);
	auto msg     = port->makeMessage();
	msg->type    = MSG_TYPE_JSON;
	msg->data    = data;
	msg->size    = 128;
	port->send(1, msg);
}

template <typename T>
class driver : noncopyable {
	using MessageCallback = std::function<bool(PortPtr,MessagePtr&)>;
public:
	driver() 
	{ 
		messageCallback_ = std::bind(&driver::callback_init, this, std::placeholders::_1, std::placeholders::_2);
	}
	virtual ~driver()
	{

	}
	virtual void init(PortPtr port, MessagePtr& message)
	{

	}
	virtual bool receive(PortPtr port, MessagePtr& message)
	{
		return true;
	}
	
	virtual void release(PortPtr port, MessagePtr& message)
	{

	}


	bool callback(PortPtr port, MessagePtr& message)
	{
		return messageCallback_(port, message);
	}

private:
	bool callback_init(PortPtr port, MessagePtr& message)
	{
		port->setOwner(message->source);
		init(port, message);
		messageCallback_ = std::bind(&driver::callback_recv, this, std::placeholders::_1, std::placeholders::_2);
		return true;
	}
	bool callback_recv(PortPtr port, MessagePtr& message)
	{
		return receive(port, message);
	}
	MessageCallback messageCallback_;
};



#define reg(T)\
class T;\
extern "C" {\
\
void* driver_create()\
{\
	void* d = static_cast<void*>(new T());\
	return d;\
}\
\
bool driver_callback(PortPtr port, void* entry, MessagePtr message)\
{\
	T* d = static_cast<T*>(entry);\
	return d->callback(port, message);\
}\
\
void driver_release(PortPtr port, void* entry, MessagePtr message)\
{\
	T* d = static_cast<T*>(entry);\
	d->release(port, message);\
	delete d;\
}\
\
}\

#endif