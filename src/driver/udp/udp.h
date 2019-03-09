#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stdlib.h>

struct UdpPacket {
	enum T { kOpen=1, kSend=2, kRecv=3, kClose=4, kOpts=5, kStatus=6, kInfo=7 };
	T type;
	uint32_t id;
	uint32_t session;
	union {
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
		} open;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
			int  nbyte;
			char data[1];
		} send;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
			int  nbyte;
			char data[1];
		} recv;
		struct {
			uint32_t optsbits;
			bool     reuseaddr;
			bool     reuseport;
			bool     active;
			uint32_t owner;
			bool     read;
		} opts;
		struct {
			bool online;
		} status;
		struct {
			char ip[64];
			uint16_t port;
			bool ipv6;
			char start[32];
			uint32_t owner;
			uint32_t readCount;
			uint32_t readBuff;
			uint32_t writeCount;
			uint32_t writeError;
		} info;
	};
};


#endif

