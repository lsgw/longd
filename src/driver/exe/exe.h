#ifndef EXE_H
#define EXE_H

#include <stdint.h>
#include <stdlib.h>

struct ExePacket {
	enum T { kOpen=1, kWrite=2, kRead=3, kClose=4, kStatus=5, kOpts=6, kInfo=7 };
	T type;
	uint32_t id;
	uint32_t session;
	union {
		struct {
			int len;
			char path[1];
		} open;
		struct {
			int nbyte;
			char data[1];
		} write;
		struct {
			int nbyte;
			char data[1];
		} read;
		struct {
			pid_t pid;
			bool online;
		} status;
		struct {
			uint32_t optsbits;
			bool     active;
			uint32_t owner;
			bool     read;
		} opts;
		struct {
			uint32_t owner;
			uint32_t readCount;
			uint32_t readBuff;
			uint32_t writeCount;
			uint32_t writeBuff;
			char start[32];
			int nbyte;
			char data[1];
		} info;
	};
};

#endif