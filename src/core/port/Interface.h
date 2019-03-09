#ifndef INTERFACE_H
#define INTERFACE_H
#include "noncopyable.h"
#include "RWLock.h"
#include "Types.h"
#include <string>
#include <memory>
#include <vector>
#include <map>

typedef void*(*interface_create)(void);
typedef bool(*interface_callback)(PortPtr port, void* entry, MessagePtr message);
typedef void(*interface_release)(PortPtr port, void* entry, MessagePtr message);

class Interface : public noncopyable, public std::enable_shared_from_this<Interface> {
public:
	Interface(const std::string& name, void* dl, interface_create create, interface_callback callback, interface_release release);
	~Interface();

	void* create() { return create_(); }
	bool  callback(PortPtr port, void* entry, MessagePtr message) { return callback_(port, entry, message); }
	void  release(PortPtr port, void* entry, MessagePtr message) { release_(port, entry, message); }
	
	void* dl() const { return dl_; }
	const std::string& name() const { return name_; }
private:
	const std::string  name_;
	void*              dl_;

	interface_create   create_;
	interface_callback callback_;
	interface_release  release_;
};


class InterfaceList : public noncopyable {
public:
	void setInterfacePath(const std::vector<std::string>& pathname);
	InterfacePtr query(const std::string& name);
private:
	RWLock lock_;
	std::vector<std::string> paths_;
	std::map<std::string,std::weak_ptr<Interface>> interfaces_;
};




#endif