#ifndef ENV_H
#define ENV_H

#include <vector>
#include <string>

struct Env {
	uint32_t                 workerThread;
	std::vector<std::string> driverPath;
	std::vector<std::string> modulePath;
	bool                     daemon;

	// start
	std::string              start;
	std::string              args;

	std::string lua_path;
	std::string lua_cpath;
	std::string lua_loader;

	uint32_t loghandle;
	uint32_t sigid;
	bool     profile;
};

#endif
