#ifndef TIMER_H
#define TIMER_H

#include "MutexLock.h"
#include <functional>

#define TIME_NEAR_SHIFT 8
#define TIME_LEVEL_SHIFT 6

#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)

#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

using TimerCallback = std::function<void(uint32_t context, uint32_t session)>;

class Timer {
	struct Node {
		uint32_t context;
		uint32_t session;
		uint32_t expire;
		Node* next;
	};
	struct List {
		Node  head;
		Node* tail;
	};
public:
	Timer();
	void timeout(uint32_t context, uint32_t time, uint32_t session);
	void updatetime();
	void setTimerCallback(const TimerCallback& cb) { timerCallback_ = cb; }
private:
	
	void move(int level, int idx);
	void dispatch(Node* node);
	void execute();
	void shift();
	void add(Node* node);
	Node* clear(List* list);
	void  link(List* list, Node* node);

	List near_[TIME_NEAR];
	List t_[4][TIME_LEVEL]; 

	MutexLock mutex_;
	
	uint32_t time_;
	uint32_t starttime_;
	uint64_t current_;
	uint64_t currentPoint_;

	TimerCallback timerCallback_;
};

#endif