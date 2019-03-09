#include "Timer.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>

Timer::Timer() : 
	time_(0)
{
	for (int i=0; i<TIME_NEAR; i++) {
		clear(&near_[i]);
	}
	for (int i=0; i<4; i++) {
		for (int j=0; j<TIME_LEVEL; j++) {
			clear(&t_[i][j]);
		}
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	starttime_    = tv.tv_sec;
	current_      = tv.tv_usec / 10000;
	currentPoint_ = (uint64_t)tv.tv_sec * 100 + (uint64_t)tv.tv_usec / 10000;
}

void Timer::timeout(uint32_t context, uint32_t time, uint32_t session)
{
	if (time <= 0) {
		timerCallback_(context, session);
	} else {
		MutexLockGuard lock(mutex_);

		auto node = new Node;
		node->context = context;
		node->session = session;
		node->expire  = time_ + time;

		add(node);
	}
}

void Timer::add(Node* node)
{
	uint32_t time = node->expire;
	uint32_t currentTime = time_;
	

	if ((time|TIME_NEAR_MASK) == (currentTime|TIME_NEAR_MASK)) {
		link(&near_[time & TIME_NEAR_MASK], node);
	} else {
		int i = 0;
		uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;

		for (i=0; i<3; i++) {
			if ((time|(mask-1)) == (currentTime|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}

		link(&t_[i][(time>>(TIME_NEAR_SHIFT + i * TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK], node);
	}
}

void Timer::updatetime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 100 + (uint64_t)tv.tv_usec / 10000;

	if (cp < currentPoint_) {
		currentPoint_ = cp;
	} else if (cp != currentPoint_) {
		uint32_t diff = (uint32_t)(cp - currentPoint_);
		currentPoint_ = cp;
		current_ += diff;

		for (uint32_t i=0; i<diff; i++) {
			execute();
			shift();
			execute();
		}
	}
}

void Timer::dispatch(Node* node)
{
	do {
		Node* temp = node;
		node = node->next;
		timerCallback_(temp->context, temp->session);
		delete temp;
	} while (node);
}

void Timer::execute()
{
	mutex_.lock();
	uint32_t idx = time_ & TIME_NEAR_MASK;
	while (near_[idx].head.next) {
		Node* node = clear(&near_[idx]);
		mutex_.unlock();
		
		dispatch(node);
		
		mutex_.lock();
	}
	mutex_.unlock();
}
void Timer::shift()
{
	MutexLockGuard lock(mutex_);

	int mask = TIME_NEAR;
	uint32_t ct = ++time_;
	if (ct == 0) {
		move(3, 0);
	} else {
		uint32_t time = ct >> TIME_NEAR_SHIFT;
		int i = 0;
		while ((ct & (mask-1)) == 0) {
			int idx = time & TIME_LEVEL_MASK;
			if (idx != 0) {
				move(i, idx);
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
			time >>= TIME_LEVEL_SHIFT;
			++i;
		}
	}
}

void Timer::move(int level, int idx)
{
	Node* node = clear(&t_[level][idx]);
	while (node) {
		Node* temp = node->next;
		add(node);
		node = temp;
	}
}

Timer::Node* Timer::clear(List* list)
{
	Node* ret = list->head.next;
	list->head.next = 0;
	list->tail = &(list->head);
	return ret;
}
void Timer::link(List* list, Node* node)
{
	list->tail->next = node;
	list->tail = node;
	node->next = 0;
}

