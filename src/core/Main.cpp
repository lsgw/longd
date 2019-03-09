#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "Scheduler.h"
#include "Env.h"
#include "utils.h"
#include <stdio.h>

using namespace std;
using namespace rapidjson;

int main(int argc, char* argv[])
{
	Env env;
	{
		if (argc < 2) {
			fprintf(stderr, "Need a config file. usage: targetssnet configfilename\n");
			return 1;
		}
		
		std::string configPath(argv[1]);
		if (configPath.empty()) {
			fprintf(stderr, "Need a config file. usage: targetssnet configfilename\n");
			return 2;
		}

		ifstream ifs(configPath);
		if (!ifs.is_open()) {
			fprintf(stderr, "open config error\n");
			return 3;
		}
		std::ostringstream tmp;
		tmp << ifs.rdbuf();
		string str = tmp.str();
		ifs.close();

		Document document;
		document.Parse(str.c_str());
		assert(document.IsObject());
		assert(document.HasMember("workerThread"));
		assert(document.HasMember("modulePath"));
		assert(document.HasMember("driverPath"));
		assert(document.HasMember("daemon"));
		assert(document.HasMember("lua_path"));
		assert(document.HasMember("lua_cpath"));
		assert(document.HasMember("lua_loader"));
		assert(document.HasMember("start"));
		assert(document.HasMember("args"));
		assert(document.HasMember("profile"));

		assert(document["workerThread"].IsInt());
		assert(document["modulePath"].IsString());
		assert(document["driverPath"].IsString());
		assert(document["daemon"].IsBool());
		assert(document["lua_path"].IsString());
		assert(document["lua_cpath"].IsString());
		assert(document["lua_loader"].IsString());
		assert(document["start"].IsString());
		assert(document["args"].IsObject());
		assert(document["profile"].IsBool());

		int workerThread  = document["workerThread"].GetInt();
		string modulePath = document["modulePath"].GetString();
		string driverPath = document["driverPath"].GetString();
		bool daemon       = document["daemon"].GetBool();
		string lua_path   = document["lua_path"].GetString();
		string lua_cpath  = document["lua_cpath"].GetString();
		string lua_loader = document["lua_loader"].GetString();
		string start      = document["start"].GetString();
		env.profile       = document["profile"].GetBool();
		
        Value& val = document["args"];
        StringBuffer sbBuf;
        Writer<StringBuffer> jWriter(sbBuf);
        val.Accept(jWriter);
        string args = std::string(sbBuf.GetString());
        

		if (daemon) {
			if (utils::becomeDaemon() < 0) {
				fprintf(stderr, "become daemon error\n");
				return 9;
			} 
			env.daemon = daemon;
		}
		if (workerThread > 0) {
			env.workerThread = workerThread;
		} else {
			fprintf(stderr, "Need workerThread > 0\n");
			return 8;
		}
		if (!modulePath.empty()) {
			env.modulePath = utils::split(modulePath, ";");
		} else {
			fprintf(stderr, "module path empty\n");
			return 4;
		}
		if (!driverPath.empty()) {
			env.driverPath = utils::split(driverPath, ";");
		} else {
			fprintf(stderr, "driver path empty\n");
			return 5;
		}

		if (!lua_path.empty()) {
			// fprintf(stderr, "lua_path: %s\n", lua_path.c_str());
			env.lua_path = lua_path;
		} else {
			fprintf(stderr, "lua_path empty\n");
			return 6;
		}

		if (!lua_cpath.empty()) {
			// fprintf(stderr, "lua_cpath: %s\n", lua_cpath.c_str());
			env.lua_cpath = lua_cpath;
		} else {
			fprintf(stderr, "lua_cpath empty\n");
			return 6;
		}

		if (!lua_loader.empty()) {
			// fprintf(stderr, "lua_loader: %s\n", lua_loader.c_str());
			env.lua_loader = lua_loader;
		} else {
			fprintf(stderr, "lua_loader empty\n");
			return 6;
		}

		if (!start.empty()) {
			env.start = start;
			// fprintf(stderr, "startService: %s\n", startService.c_str());
		} else {
			fprintf(stderr, "start service empty\n");
			return 6;
		}

		if (!args.empty()) {
			env.args = args;
			// fprintf(stderr, "startArgs: %s\n", startArgs.c_str());
		} else {
			fprintf(stderr, "start args empty\n");
			return 7;
		}
	}
	Scheduler s;
	s.start(env);

	return 0;
}