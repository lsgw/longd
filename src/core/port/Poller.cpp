#include "Poller.h"
#include "EventLoop.h"
#include "Channel.h"
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>


#ifdef __MACH__
#include <sys/event.h>

Poller::Poller() :
	kfd_(::kqueue()),
	events_(32)
{
	if (kfd_ < 0) {
		::fprintf(stderr, "kqueue create error, reason: %s", strerror(errno));
		::abort();
	}
}

Poller::~Poller()
{
	::close(kfd_);
}

void Poller::poll(std::vector<Channel*>* activeChannels, int waitms)
{
	struct timespec timeout;
	timeout.tv_sec = waitms / 1000;
	timeout.tv_nsec = (waitms % 1000) * 1000 * 1000;
	int n = ::kevent(kfd_, NULL, 0, &(*events_.begin()), events_.size(), &timeout);
	for (int i=0; i<n; i++) {
		Channel* ch = static_cast<Channel*>(events_[i].udata);
		int      fd = static_cast<int>(events_[i].ident);
		assert(fd == ch->fd());
		
		// printf("Poller::poll fd = %d\n", fd);
		int ev = 0;
		if (events_[i].filter == EVFILT_EXCEPT) {
			ev |= Channel::kErrorEvent;
		}
		if (events_[i].filter == EVFILT_WRITE) {
			ev |= Channel::kWriteEvent;
		}
		if (events_[i].filter == EVFILT_READ) {
			ev |= Channel::kReadEvent;
		}
		ch->set_revent(ev);
		activeChannels->push_back(ch);
	}
	if (n == static_cast<int>(events_.size())) {
		events_.resize(events_.size() * 2);
	}
}
void Poller::updateChannel(Channel* ch)
{
	struct kevent e[2];
	bzero(e, sizeof(e));

	int fd = ch->fd();

	if (ch->isReading()) {
		// printf("Poller add read = %d\n", fd);
		EV_SET(&e[0], fd, EVFILT_READ,  EV_ADD,    0, 0, (void*)ch);
	} else {
		// printf("Poller del read = %d\n", fd);
		EV_SET(&e[0], fd, EVFILT_READ,  EV_DELETE, 0, 0, (void*)ch);
	}
	if (ch->isWriting()) {
		// printf("Poller add write = %d\n", fd);
		EV_SET(&e[1], fd, EVFILT_WRITE, EV_ADD,    0, 0, (void*)ch);
	} else {
		// printf("Poller del write = %d\n", fd);
		EV_SET(&e[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, (void*)ch);
	}

	::kevent(kfd_, &e[0], 1, NULL, 0, NULL);
	::kevent(kfd_, &e[1], 1, NULL, 0, NULL);

	//::kevent(kfd_, e, 2, NULL, 0, NULL);
}

#else

#include <sys/epoll.h>
#include <string.h>

Poller::Poller() :
	efd_(::epoll_create(256)),
	events_(32)
{
	if (efd_ < 0) {
		fprintf(stderr, "epoll create error, reason: %s", strerror(errno));
		abort();
	}
}

Poller::~Poller()
{
	::close(efd_);
}

void Poller::poll(std::vector<Channel*>* activeChannels, int waitms)
{
	int n = ::epoll_wait(efd_, &*events_.begin(), static_cast<int>(events_.size()), waitms);
	for (int i=0; i<n; i++) {
		Channel* ch = static_cast<Channel*>(events_[i].data.ptr);
		int      fd = ch->fd();
		
		auto it = channels_.find(fd);
		assert(it != channels_.end());
		assert(it->second == ch);

		int ev = 0;
		if (events_[i].events & EPOLLERR) {
			ev |= Channel::kErrorEvent;
		}
		if (events_[i].events & EPOLLOUT) {
			ev |= Channel::kWriteEvent;
		}
		if (events_[i].events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
			ev |= Channel::kReadEvent;
		}
		ch->set_revent(ev);
		activeChannels->push_back(ch);
	}
	if (n == static_cast<int>(events_.size())) {
		events_.resize(events_.size() * 2);
	}
}
void Poller::updateChannel(Channel* ch)
{
	struct epoll_event e;
	bzero(&e, sizeof(e));

	int fd = ch->fd();
	e.data.ptr = ch;

	if (hasChannel(ch)) {
		if (ch->isNoneEvent()) {
			e.events |= EPOLLIN;
			e.events |= EPOLLOUT;
			::epoll_ctl(efd_, EPOLL_CTL_DEL, fd, &e);
			channels_.erase(fd);
		} else {
			if (ch->isReading()) {
				e.events |= EPOLLIN;
			}
			if (ch->isWriting()) {
				e.events |= EPOLLOUT;
			}
			::epoll_ctl(efd_, EPOLL_CTL_MOD, fd, &e);
		}
	} else {
		if (!ch->isNoneEvent()) {
			if (ch->isReading()) {
				e.events |= EPOLLIN;
			}
			if (ch->isWriting()) {
				e.events |= EPOLLOUT;
			}
			::epoll_ctl(efd_, EPOLL_CTL_ADD, fd, &e);

			channels_.insert({fd, ch});
		}
	}
}

bool Poller::hasChannel(Channel* ch) const
{
	auto it = channels_.find(ch->fd());
	return it != channels_.end() && it->second == ch;
}




#endif