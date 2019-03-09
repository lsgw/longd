#include "Interface.h"
#include <stdio.h>
#include <dlfcn.h>


Interface::Interface(const std::string& name, void* dl, interface_create create, interface_callback callback, interface_release release) :
	name_(name),
	dl_(dl),
	create_(create),
	callback_(callback),
	release_(release)
{
	fprintf(stderr, "create  - %s\n", name_.c_str());
}

Interface::~Interface()
{
	fprintf(stderr, "destroy - %s\n", name_.c_str());
}

void InterfaceList::setInterfacePath(const std::vector<std::string>& pathname)
{
	paths_.clear();
	paths_ = pathname;
	for (auto& path : paths_) {
		fprintf(stderr, "driver path - %s\n", path.c_str());
	}
}

std::shared_ptr<Interface> InterfaceList::query(const std::string& name)
{
	std::shared_ptr<Interface> m;
	
	lock_.rdlock();
	auto it = interfaces_.find(name);
	if (it != interfaces_.end() && !it->second.expired()) {
		m = it->second.lock();
		
	} else {
		
	}
	lock_.unlock();

	if (m) {
		return m;
	}

	for (auto pathname : paths_) {
		pathname.replace(pathname.find("?"), 1, name);
		
		void* dl = dlopen(pathname.c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (dl) {
			
			interface_create   create   = (interface_create)  dlsym(dl, "driver_create");
			interface_callback callback = (interface_callback)dlsym(dl, "driver_callback");
			interface_release  release  = (interface_release) dlsym(dl, "driver_release");
			
			if (create && callback && release) {
				
				m.reset(new Interface(name, dl, create, callback, release), [this](Interface* p) {
					
					lock_.wrlock();
					interfaces_.erase(p->name());
					lock_.unlock();

					dlclose(p->dl());
					
					delete p;
				});

				lock_.wrlock();
				interfaces_[name] = std::weak_ptr<Interface>(m);
				lock_.unlock();
				
				return m;
			} else {
				
			}
		} else {
			
		}
	}
	

	return m;
}