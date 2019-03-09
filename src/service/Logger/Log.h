#ifndef LOG_H
#define LOG_H

#include "Types.h"
#include "Context.h"


class Log {
public:
	const static int TRACE = 0;  //指出比DEBUG粒度更细的一些信息事件 (开发过程使用)
	const static int DEBUG = 1;  //指出细粒度信息事件对调试应用程序是非常有帮助（开发过程使用)
	const static int INFO  = 2;  //表明消息在粗粒度级别上突出强调应用程序的运行过程
	const static int WARN  = 3;  //系统能正常运行，但可能会出现潜在错误的情形
	const static int ERROR = 4;  //指出虽然发生错误事件，但仍然不影响系统的继续运行
	const static int FATAL = 5;  //指出每个严重的错误事件将会导致应用程序的退出
	
	Log() : 
		ctx(NULL), 
		handle(0) 
	{

	}
	Log(Context* c, uint32_t h) :
		ctx(c),
		handle(h)
	{

	}
	void write(int level, const std::string& message)
	{
		int    size = sizeof(int) + message.size();
		char*  data = (char*)malloc(size);
		*(int*)data = level;

		strncpy(data+sizeof(int), message.data(), message.size());
		
		auto msg  = ctx->makeMessage();
		msg->type = MSG_TYPE_LOG;
		msg->data = data;
		msg->size = size;
		ctx->send(handle, msg);
	}

	void trace(const std::string& message)
	{
		write(TRACE, message);
	}
	void debug(const std::string& message)
	{
		write(DEBUG, message);
	}
	void info(const std::string& message)
	{
		write(INFO, message);
	}
	void warn(const std::string& message)
	{
		write(WARN, message);
	}
	void error(const std::string& message)
	{
		write(ERROR, message);
	}
	void fatal(const std::string& message)
	{
		write(FATAL, message);
	}

	Context* ctx;
	uint32_t handle;
};

#endif