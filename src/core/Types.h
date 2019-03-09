#ifndef TYPES_H
#define TYPES_H

#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include <map>

#define MSG_TYPE_EXIT    1
#define MSG_TYPE_TIME    2
#define MSG_TYPE_EVENT   3
#define MSG_TYPE_JSON    4
#define MSG_TYPE_LUA     5
#define MSG_TYPE_LOG     6

#define MSG_TYPE_TCP     7
#define MSG_TYPE_UDP     8
#define MSG_TYPE_SIG     9
#define MSG_TYPE_EXE    10

#define MSG_TYPE_TRACE  11
#define MSG_TYPE_DEBUG  12


class Module;
class Context;
class Interface;
class Port;

struct Message final {
	Message() : mid(0), source(0), type(0), data(NULL), size(0) { }
	~Message() { if (data && size) { free(data); } }
	uint32_t mid;
	uint32_t source;
	uint32_t type;
	void*    data;
	uint32_t size;
};
struct Event {
	enum T { kUnknow, kRead, kWrite, kError };
	T type;
	int fd;
};

using MessagePtr   = std::shared_ptr<Message>;
using ModulePtr    = std::shared_ptr<Module>;
using ContextPtr   = std::shared_ptr<Context>;
using InterfacePtr = std::shared_ptr<Interface>;
using PortPtr      = std::shared_ptr<Port>;

#endif