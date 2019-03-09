#ifndef SIG_H
#define SIG_H

#include <stdint.h>
#include <stdlib.h>

struct SigPacket {
	enum T { kActionModule=1, kActionDriver=2, kCancelModule=3, kCancelDriver=4, kHappen=5, kActionCHLD=6, kCancelCHLD=7, kHappenCHLD=8, kInfo=9 };
	T type;
	union {
		struct {
			uint32_t handle;
			uint32_t sig;
		} actionModule;
		struct {
			uint32_t id;
			uint32_t sig;
		} actionDriver;
		struct {
			uint32_t handle;
			uint32_t sig;
		} cancelModule;
		struct {
			uint32_t id;
			uint32_t sig;
		} cancelDriver;
		struct {
			uint32_t sig;
		} happen;

		struct {
			uint32_t id;
			pid_t pid;
		} actionCHLD;
		struct {
			uint32_t id;
			pid_t pid;
		} cancelCHLD;
		struct {
			pid_t pid;
		} happenCHLD;
		struct {
			int nbyte;
			char data[1];
		} info;
	};
};

#endif