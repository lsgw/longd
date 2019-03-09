#ifndef POLLER_H
#define POLLER_H

#include "noncopyable.h"
#include <vector>
#include <map>
class Channel;

#ifdef __MACH__
class Poller : noncopyable {
public:
	Poller();
	~Poller();
	
	void poll(std::vector<Channel*>* activeChannels, int waitms);
	void updateChannel(Channel* ch);
private:
	int kfd_;
	std::vector<struct kevent> events_;
};

#else

class Poller : noncopyable {
public:
	Poller();
	~Poller();
	
	void poll(std::vector<Channel*>* activeChannels, int waitms);
	void updateChannel(Channel* ch);
private:
	bool hasChannel(Channel* channel) const;
	int efd_;
	std::vector<struct epoll_event> events_;
	std::map<int, Channel*> channels_;
};


#endif



#endif