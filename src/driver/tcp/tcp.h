#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stdlib.h>

struct TcpPacket {
	enum T { kListen=1, kAccept=2, kConnect=3, kRead=4, kWrite=5, kShutdown=6, kClose=7, kOpts=8, kStatus=9, kShift=10, kLowWaterMark=11, kHighWaterMark=12, kInfo=13 };
	T type;
	uint32_t id;
	uint32_t session;
	union {
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
		} listen;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
			uint32_t id;
		} accept;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
		} connect;
		struct {
			int nbyte;
			char data[1];
		} read;
		struct {
			int nbyte;
			char data[1];
		} write;
		struct {
			uint32_t optsbits;
			bool     reuseaddr;
			bool     reuseport;
			bool     keepalive;
			bool     nodelay;
			bool     active;
			uint32_t owner;
			bool     read;
		} opts;
		struct {
			bool online;
			uint32_t id;
		} status;
		struct {
			int fd;
		} shift;
		struct {
			bool on;
			uint64_t value;
		} lowWaterMark;
		struct {
			bool on;
			uint64_t value;
		} highWaterMark;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
			
			char start[32];
			uint32_t owner;
			uint32_t readCount;
			uint32_t readBuff;
			uint32_t writeCount;
			uint32_t writeBuff;
		} info;
	};
};

#endif