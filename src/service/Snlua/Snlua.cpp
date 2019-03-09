#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "nlohmann/json.hpp"
#include "Logger/Log.h"
#include "Actor.h"
#include "utils.h"
#include "lua/lua.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <set>
#include "tcp.h"

using namespace rapidjson;

void getCallInfo(lua_State* L, lua_Debug* ar, std::pair<std::string, uint32_t>* breakingPointInfo, std::pair<std::string, std::set<uint32_t>>* callFunInfo)
{
	int top = lua_gettop(L);
	lua_getinfo(L, "lSL", ar);
	std::string pathname(ar->short_src);
	std::string filename = pathname.substr(pathname.find_last_of('/')+1);
	uint32_t    fileline = ar->currentline;

	breakingPointInfo->first  = filename;
	breakingPointInfo->second = fileline;

	callFunInfo->first = filename;
	callFunInfo->second.clear();
	
	assert(lua_type(L, -1) == LUA_TTABLE);

	lua_pushnil(L);
	while(lua_next(L, -2)) {
		 assert(lua_type(L, -2) == LUA_TNUMBER);
		 assert(lua_type(L, -1) == LUA_TBOOLEAN);
		 uint32_t line = (uint32_t)lua_tonumber(L, -2);
		 bool ok = (bool)lua_toboolean(L, -1);
		 if (ok) {
		 	callFunInfo->second.insert(line);
		 }
		 lua_pop(L, 1);
	}
	lua_settop(L, top);
}

std::string getTopTable(lua_State* L)
{
	assert(lua_type(L, -1) == LUA_TTABLE);

	std::string table = "{";
	int top = lua_gettop(L);

	lua_pushnil(L);
	while(lua_next(L, -2)) {
		char key[128] = { '\0' };
		int type = lua_type(L, -2);
        switch (type) {
	    case LUA_TNUMBER:
            snprintf(key, sizeof(key), "%g:", lua_tonumber(L, -2));
            break;
        case LUA_TBOOLEAN:
            snprintf(key, sizeof(key), "%s:", lua_toboolean(L, -2)?"true":"false");
            break;
        case LUA_TSTRING:
            snprintf(key, sizeof(key), "%s:", lua_tostring(L, -2));
            break;
        default:
            snprintf(key, sizeof(key), "%p(%s):", lua_topointer(L, -2), lua_typename(L, type));
            break;
        }
        table.append(key);

        char value[128] = { '\0' };
        type = lua_type(L, -1);
        switch (type) {
        case LUA_TNUMBER:
            snprintf(value, sizeof(value), "%g", lua_tonumber(L, -1));
            table.append(value);
            break;
        case LUA_TBOOLEAN:
            snprintf(value, sizeof(value), "%s", lua_toboolean(L, -1)?"true":"false");
            table.append(value);
            break;
        case LUA_TSTRING:
            snprintf(value, sizeof(value), "%s", lua_tostring(L, -1));
            table.append(value);
            break;
        case LUA_TTABLE:
            table.append(getTopTable(L));
            break;
        default:
            snprintf(value, sizeof(value), "%p(%s)", lua_topointer(L, -1), lua_typename(L, type));
            table.append(value);
            break;
        }
        table.append(",");
        lua_pop(L, 1);
	}

	lua_settop(L, top);
	table[table.size()-1] = '}';

	return table;
}

std::string getTopValue(lua_State* L)
{
	std::string content;
	char buf[128] = { '\0' };
	int type = lua_type(L, -1);
    switch (type) {
	    case LUA_TNUMBER:
	        snprintf(buf, sizeof(buf), "%g", lua_tonumber(L, -1));
	        content.append(buf);
	        break;
	    case LUA_TBOOLEAN:
	        snprintf(buf, sizeof(buf), "%s", lua_toboolean(L, -1)? "true": "false");
	        content.append(buf);
	        break;
	    case LUA_TSTRING:
	        snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1));
	        content.append(buf);
	        break;
	    case LUA_TTABLE:
	        content.append(getTopTable(L));
	        break;
	    default:
	        snprintf(buf, sizeof(buf), "%p(%s)", lua_topointer(L, -2), lua_typename(L, type));
            content.append(buf);
            break;
    }

    return content;
}


class Snlua : public Actor<Snlua> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		log.ctx    = ctx.get();
		log.handle = ctx->env().loghandle;

		std::string lua_path   = ctx->env().lua_path;
		std::string lua_cpath  = ctx->env().lua_cpath;
		std::string lua_loader = ctx->env().lua_loader;
	
		// log.trace(lua_path);
		// log.trace(lua_cpath);
		// log.trace(lua_loader);
		// log.trace(std::string(static_cast<char*>(message->data), message->size).c_str());

		L = luaL_newstate();
		lua_gc(L, LUA_GCSTOP, 0);
		luaL_openlibs(L);
		lua_pushlightuserdata(L, ctx.get());
		lua_setfield(L, LUA_REGISTRYINDEX, "context");
		
		lua_pushstring(L, lua_path.c_str());
		lua_setglobal(L, "LUA_PATH");

		lua_pushstring(L, lua_cpath.c_str());
		lua_setglobal(L, "LUA_CPATH");

		lua_sethook(L, Snlua::hook, LUA_MASKCOUNT, 4000);

		int r = luaL_loadfile(L, lua_loader.c_str());
		if (r != LUA_OK) {
			log.info("Can't load " + lua_loader + " : " + lua_tostring(L, -1));
			ctx->exit();
			return;
		}

		lua_pushlstring(L, static_cast<char*>(message->data), message->size);
		r = lua_resume(L, NULL, 1);
		if (r == 0) {
			log.info(std::string("init to exit"));
			ctx->exit();
			return;
		}
		if (r != LUA_YIELD) {
			log.info(std::string("lua loader error:\n") + lua_tostring(L, -1));
			ctx->exit();
			return;
		}
		bool match = true;
		int top = lua_gettop(L);
		if (top == 0) {
			//printf("[%010d] init receive hook lua_yield = %d\n", ctx->handle(), (int)match);
		} else {
			assert(top == 2);
			assert(lua_isboolean(L, 1));
			match = lua_toboolean(L, 1);
			uint32_t mid = (uint32_t)luaL_checkinteger(L, 2);
			assert(mid == 0);

			//printf("[%010d] init receive main lua_yield = %d\n", ctx->handle(), (int)match);
		}
		
		lua_sethook(L, NULL, 0, 0);

		lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
		lua_State *gL = lua_tothread(L,-1);
		assert(L == gL);

		lua_settop(L, 0);
		lua_gc(L, LUA_GCRESTART, 0);
	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		Value array(kArrayType);
		array.PushBack("cast", allocator);
		array.PushBack("ref",  allocator);
		array.PushBack(ctx->handle(), allocator);
		array.PushBack("exit", allocator);
		array.PushBack(ctx->handle(), allocator);

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		array.Accept(writer);
		std::string args = buffer.GetString();

		char* data = (char*)malloc(args.size());
		memcpy(data, args.data(), args.size());

		auto msg    = ctx->makeMessage();
		msg->source = ctx->handle();
		msg->type   = MSG_TYPE_JSON;
		msg->data   = data;
		msg->size   = args.size();
		ctx->send(1, msg);

		lua_close(L);
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{
		// printf("[:%08x] source(%08x) type(%d) data(%p) size(%d) pending = %u\n", ctx->handle(), message->source, message->type, message->data, message->size, ctx->mailboxLength());
		
		lua_sethook(L, Snlua::hook, hookMask, hookCount);
		
		if (message->type == MSG_TYPE_DEBUG) {
			return debug(ctx, static_cast<const char*>(message->data), message->size);
		}
		if (breakingPoint) {
			return false;
		}

		int r = 0;
		if (message->type == 0) {
			r = lua_resume(L, NULL, 0);
			fcout++;
		} else {
			uint32_t mid = ctx->save(message);
			lua_pushinteger(L, mid);
			r = lua_resume(L, NULL, 1);
			fcout = 0;
		}
		if (r == 0) {
			ctx->exit();
			return true;
		}
		if (r != LUA_YIELD) {
			std::string callstack;
			int level = 0;
			lua_Debug ar;
			while (lua_getstack(L, level, &ar)) {
				lua_getinfo(L, "nlS", &ar);
				std::string filename = ar.source? ar.source : "null";
				std::string funcname = ar.name? ar.name : "null" ;
				std::string fileline = std::to_string(ar.currentline);
				callstack.append(std::to_string(level) + "  " + filename + "  " + funcname + "  " + fileline + "\n");
				level++;
			}
			log.info(std::string("[lua vm error] ") + lua_tostring(L, -1) + "\n" + callstack);
			ctx->exit();
			return true;
		}


		bool match = true;
		int top = lua_gettop(L);
		if (top == 0) {
			// printf("[:%08x] receive hook lua_yield = %d\n", ctx->handle(), (int)match);
		} else {
			// printf("[:%08x] receive main lua_yield = %d\n", ctx->handle(), (int)match);
			assert(top == 2);
			assert(lua_isboolean(L, 1));
			match = lua_toboolean(L, 1);
			if (!match) {
				uint32_t mid = (uint32_t)luaL_checkinteger(L, 2);
				message = ctx->get(mid);
				assert(message.get() != NULL);
				ctx->free(mid);
			}
		}
		if (fcout > 0) {
			char buf[128];
			snprintf(buf, sizeof(buf), "handle %08x maybe in an endless loop", ctx->handle());
			// log.warn(buf);
			fcout = 0;
		}
		lua_settop(L, 0);
		lua_sethook(L, 0, 0, 0);
		return match;
	}


	bool hasBreakingPoint(Context* ctx, lua_Debug* ar)
	{
		// printf("[%08x] lua_getinfo src=%s line = %d\n", ctx->handle(), ar->short_src, ar->currentline);
		std::pair<std::string, uint32_t> tempBreakingPointInfo;
		currBreakingPointInfo.first  = "";
		currBreakingPointInfo.second = 0;
		prevBreakingPointInfo.first  = "";
		prevBreakingPointInfo.second = 0;
		currCallFunInfo.second.clear();
		prevCallFunInfo.second.clear();

		bool breaking = false;
		std::string pathname(ar->short_src);
		std::string filename = pathname.substr(pathname.find_last_of('/')+1);
		uint32_t    fileline = ar->currentline;
		for (auto m : currDebugBreakingPointInfo) {
			if (m.first == filename && m.second == fileline) {
				tempBreakingPointInfo = m;
				breaking = true;
				// printf("[%08x] next1 match break %s:%u\n", ctx->handle(), tempBreakingPointInfo.first.c_str(), tempBreakingPointInfo.second);
				break;
			}
		}
		if (!breaking && debugActionType == kNext) {
			for (auto m : prevDebugBreakingPointInfo) {
				if (m.first == filename && m.second == fileline) {
					tempBreakingPointInfo = m;
					breaking = true;
					// printf("[%08x] next2 match break %s:%u\n", ctx->handle(), tempBreakingPointInfo.first.c_str(), tempBreakingPointInfo.second);
					break;
				}
			}
		}
		if (!breaking && debugActionType == kStep) {
			if (lua_getstack(L, 1, ar)) {
				lua_getinfo(L, "lS", ar);
				std::string pathname(ar->short_src);
				std::string filename = pathname.substr(pathname.find_last_of('/')+1);
				uint32_t    fileline = ar->currentline;
				for (auto m : prevDebugBreakingPointInfo) {
					if (m.first == filename && m.second == fileline) {
						tempBreakingPointInfo = m;
						breaking = true;
						// printf("[%08x] step1 match break %s:%u\n", ctx->handle(), tempBreakingPointInfo.first.c_str(), tempBreakingPointInfo.second);
						break;
					}
				}
			}
		}
		if (breaking) {
			if (lua_getstack(L, 0, ar)) {
				// printf("lua_getstack 0\n");
				getCallInfo(L, ar, &currBreakingPointInfo, &currCallFunInfo);
			}
			if (lua_getstack(L, 1, ar)) {
				// printf("lua_getstack 1\n");
				getCallInfo(L, ar, &prevBreakingPointInfo, &prevCallFunInfo);
			}
			nlohmann::json rets = debugList();
			nlohmann::json returnarray;
			returnarray.push_back(ctx->handle());
			returnarray.push_back(currBreakingPointInfo.first);
			returnarray.push_back(currBreakingPointInfo.second);
			for (uint32_t i=0; i<rets.size(); i++) {
				returnarray.push_back(rets[i]);
			}
			std::string rmsg = returnarray.dump();
			char* data = (char*)malloc(rmsg.size());
			memcpy(data, rmsg.data(), rmsg.size());
			auto msg     = ctx->makeMessage();
			msg->type    = MSG_TYPE_JSON;
			msg->data    = data;
			msg->size    = rmsg.size();
			ctx->send(debugWatcher, msg);
			// printf("[%08x] match break %s:%u\n", ctx->handle(), tempBreakingPointInfo.first.c_str(), tempBreakingPointInfo.second);
			// printf("[%08x] curr  break %s:%u\n", ctx->handle(), currBreakingPointInfo.first.c_str(), currBreakingPointInfo.second);
			// printf("[%08x] prev  break %s:%u\n", ctx->handle(), prevBreakingPointInfo.first.c_str(), prevBreakingPointInfo.second);
		}
		return breaking;
	}

	bool debug(ContextPtr ctx, const char* jsondata, uint32_t jsonsize)
	{
		std::string d(jsondata, jsonsize);
		printf("[%010d] %s\n", ctx->handle(), d.c_str());
		auto j = nlohmann::json::parse(std::string(jsondata, jsonsize).c_str());
		assert(j.is_array());
		assert(j.size() >= 4);
		assert(j[0].is_string());          // pattern calldebug, respdebug
		assert(j[1].is_string());          // ref
		assert(j[2].is_number_unsigned()); // source
		assert(j[3].is_string());          // function name

		std::string pattern = j[0].get<std::string>();
		std::string ref     = j[1].get<std::string>();
		uint32_t source     = j[2].get<uint32_t>();
		std::string cmd     = j[3].get<std::string>();
		nlohmann::json args;
		for (uint32_t i=4; i<j.size(); i++) {
			args.push_back(j[i]);
		}

		nlohmann::json rets;
		if (cmd == "break") {
			rets = debugBreak(ctx, args, source);
		} else if (cmd == "delete" && hookCount == kDebugHookCount) {
			rets = debugDelete(ctx, args);
		} else if (cmd == "next" && hookCount == kDebugHookCount) {
			rets = debugNext(ctx);
		} else if (cmd == "step" && hookCount == kDebugHookCount) {
			rets = debugStep(ctx);
		} else if (cmd == "list" && hookCount == kDebugHookCount) {
			rets = debugList();
		} else if (cmd == "continue" && hookCount == kDebugHookCount) {
			rets = debugContinue(ctx);
		} else if (cmd == "print" && hookCount == kDebugHookCount) {
			rets = debugPrint(ctx, args);
		} else if (cmd == "locals" && hookCount == kDebugHookCount) {
			rets = debugLocals(ctx);
		} else if (cmd == "bt" && hookCount == kDebugHookCount) {
			rets = debugBT(ctx);
		} else if (cmd == "exit" && hookCount == kDebugHookCount) {
			rets = debugExit(ctx);
		} else if (cmd == "info") {
			rets = debugInfo(ctx);
		} else if (cmd == "ping") {
			rets = debugPing(ctx);
		} else {
			nlohmann::json a;
			a.push_back("debug cmd error");
			rets = a;
		}
		nlohmann::json returnarray;
		returnarray.push_back("respdebug");
		returnarray.push_back(ref);
		returnarray.push_back(ctx->handle());
		for (uint32_t i=0; i<rets.size(); i++) {
			returnarray.push_back(rets[i]);
		}
		std::string rmsg = returnarray.dump();
		char* data = (char*)malloc(rmsg.size());
		memcpy(data, rmsg.data(), rmsg.size());
		auto msg     = ctx->makeMessage();
		msg->type    = MSG_TYPE_TRACE;
		msg->data    = data;
		msg->size    = rmsg.size();

		ctx->send(source, msg);
		return true;
	}
	nlohmann::json debugBreak(ContextPtr ctx, nlohmann::json j, uint32_t watcher)
	{
		assert(j.size() == 2);
		debugWatcher = watcher;
		std::string filename = j[0].get<std::string>();
		uint32_t    fileline = j[1].get<uint32_t>();
		currDebugBreakingPointInfo.push_back({filename, fileline});
		holdDebugBreakingPointInfo.push_back({filename, fileline});

		hookMask  = kDebugHookMask;
		hookCount = kDebugHookCount;
		
		nlohmann::json a;
		a.push_back(filename);
		a.push_back(fileline);
		return a;
	}
	nlohmann::json debugDelete(ContextPtr ctx, nlohmann::json j)
	{
		assert(j.size() == 2);
		std::string filename = j[0].get<std::string>();
		uint32_t    fileline = j[1].get<uint32_t>();
		bool ok = false;
		for (auto it = holdDebugBreakingPointInfo.begin(); it!=holdDebugBreakingPointInfo.end(); it++) {
			if (it->first == filename && it->second == fileline) {
				holdDebugBreakingPointInfo.erase(it);
				ok = true;
				break;
			}
		}
		nlohmann::json a;
		a.push_back(ok);
		return a;
	}


	nlohmann::json debugNext(ContextPtr ctx)
	{
		nlohmann::json a;
		if (breakingPoint == 1) {
			breakingPoint = 0;
			debugActionType = kNext;
			ctx->yield();
			
			currDebugBreakingPointInfo.clear();
			prevDebugBreakingPointInfo.clear();

			for (auto line : currCallFunInfo.second) {
				if (currBreakingPointInfo.second == line) {
					continue;
				}
				// printf("[%08x] next curr %s:%u\n", ctx->handle(), currCallFunInfo.first.c_str(), line);
				currDebugBreakingPointInfo.push_back({currCallFunInfo.first, line});
			}
			for (auto line : prevCallFunInfo.second) {
				// printf("[%08x] next prev %s:%u\n", ctx->handle(), prevCallFunInfo.first.c_str(), line);
				prevDebugBreakingPointInfo.push_back({prevCallFunInfo.first, line});
			}
			a.push_back("next");
		} else {
			a.push_back("next error");
		}
		
		return a;
	}
	nlohmann::json debugStep(ContextPtr ctx)
	{
		nlohmann::json a;
		if (breakingPoint == 1) {
			breakingPoint = 0;
			debugActionType = kStep;
			ctx->yield();
			
			currDebugBreakingPointInfo.clear();
			prevDebugBreakingPointInfo.clear();

			for (auto line : currCallFunInfo.second) {
				if (currBreakingPointInfo.second == line) {
					continue;
				}
				// printf("[%08x] step1 curr %s:%u\n", ctx->handle(), currCallFunInfo.first.c_str(), line);
				currDebugBreakingPointInfo.push_back({currCallFunInfo.first, line});
			}
			for (auto line : prevCallFunInfo.second) {
				// printf("[%08x] step2 curr %s:%u\n", ctx->handle(), prevCallFunInfo.first.c_str(), line);
				currDebugBreakingPointInfo.push_back({prevCallFunInfo.first, line});
			}

			for (auto line : currCallFunInfo.second) {
				// printf("[%08x] step prev %s:%u\n", ctx->handle(), currCallFunInfo.first.c_str(), line);
				prevDebugBreakingPointInfo.push_back({currCallFunInfo.first, line});
			}
			a.push_back("step");
		} else {
			a.push_back("step error");
		}
		return a;
	}
	nlohmann::json debugList()
	{
		std::string filename = currBreakingPointInfo.first;
		uint32_t    fileline = currBreakingPointInfo.second;

		uint32_t sline = fileline < 5 ? 0 : fileline - 5;
		uint32_t eline = fileline + 5;

		std::string pathname;
		lua_Debug ar;
		if (lua_getstack(L, 0, &ar)) {
			lua_getinfo(L, "S", &ar);
			if (ar.source) {
				pathname = (ar.source+1);
			}
		}

		std::fstream f(pathname);
		std::vector<std::string>  words;
		std::string      line;
		while (std::getline(f, line)) {
			words.push_back(line);  
		}
		std::string content;
		char lineno[16];
		for (uint32_t i=sline; i<eline && i < words.size(); i++) {
			if (fileline == i+1) {
				snprintf(lineno, sizeof(lineno), "%4s  ", "->");
			} else {
				snprintf(lineno, sizeof(lineno), "%4u  ", i+1);
			}
			content.append(lineno, 6);
			content.append(words[i]);
			content.append("\n");
		}
		nlohmann::json a;
		a.push_back(content);
		return a;
	}
	nlohmann::json debugContinue(ContextPtr ctx)
	{
		nlohmann::json a;
		if (breakingPoint == 1) {
			breakingPoint = 0;
			debugActionType = kNext;
			ctx->yield();

			currDebugBreakingPointInfo.clear();
			prevDebugBreakingPointInfo.clear();

			for (auto m : holdDebugBreakingPointInfo) {
				currDebugBreakingPointInfo.push_back({m.first, m.second});
			}
			a.push_back("continue");
		} else {
			a.push_back("continue error");
		}
		return a;
	}
	nlohmann::json debugPrint(ContextPtr ctx, nlohmann::json j)
	{
		assert(j.size() == 1);
		std::string varname = j[0].get<std::string>();
		std::string varvalue = "no value";

		lua_Debug ar;
		if (lua_getstack(L, 0, &ar)) {
			int top = lua_gettop(L);
			lua_getinfo(L, "f", &ar);
			const char* name = NULL;
			int j = 1;
			while ((name = lua_getupvalue(L, -1, j)) != NULL) {
				// printf("\tupvalue %d %s\n", j, name);
				if (std::string(name) == varname) {
					varvalue = getTopValue(L);
					lua_pop(L, 1);
					break;
				}
				lua_pop(L, 1);
				j++;
			}

			int i = 1;
			while ((name = lua_getlocal(L, &ar, i)) != NULL) {
				// printf("\tlocal %d %s\n", i, name);
				if (std::string(name) == varname) {
					varvalue = getTopValue(L);
					lua_pop(L, 1);
					break;
				}
				lua_pop(L, 1);
				i++;
			}
			lua_settop(L, top);
		}
		nlohmann::json a;
		a.push_back(varvalue);
		return a;
	}
	nlohmann::json debugLocals(ContextPtr ctx)
	{
		std::string locals;
		lua_Debug ar;
		if (lua_getstack(L, 0, &ar)) {
			int top = lua_gettop(L);
			lua_getinfo(L, "f", &ar);
			const char* name = NULL;
			int j = 1;
			while ((name = lua_getupvalue(L, -1, j)) != NULL) {
				// printf("\tupvalue %d %s\n", j, name);
				if (name && strcmp(name, "(*temporary)") != 0) {
					locals.append(name);
					locals.append(" : ");
					locals.append(getTopValue(L));
					locals.append("\n");
				}
				lua_pop(L, 1);
				j++;
			}

			int i = 1;
			while ((name = lua_getlocal(L, &ar, i)) != NULL) {
				// printf("\tlocal %d %s\n", i, name);
				if (name && strcmp(name, "(*temporary)") != 0) {
					locals.append(name);
					locals.append(" : ");
					locals.append(getTopValue(L));
					locals.append("\n");
				}
				lua_pop(L, 1);
				i++;
			}
			lua_settop(L, top);
		}
		nlohmann::json a;
		a.push_back(locals);
		return a;
	}
	nlohmann::json debugBT(ContextPtr ctx)
	{
		std::string bt;
		int level = 0;
		lua_Debug ar;
		while (lua_getstack(L, level, &ar)) {
			lua_getinfo(L, "nlS", &ar);
			std::string filename = ar.source? ar.source : "null";
			std::string funcname = ar.name? ar.name : "null" ;
			std::string fileline = std::to_string(ar.currentline);
			bt.append(std::to_string(level) + "  " + filename + "  " + funcname + "  " + fileline + "\n");
			level++;
		}
		nlohmann::json a;
		a.push_back(bt);
		return a;
	}

	nlohmann::json debugExit(ContextPtr ctx)
	{
		hookMask  = kNormalHookMask;
		hookCount = kNormalHookCount;
		if (breakingPoint) {
			breakingPoint = 0;
			ctx->yield();
		}
		debugWatcher = 0;
		currBreakingPointInfo.first = "";
		currCallFunInfo.first = "";
		prevCallFunInfo.first = "";

		currCallFunInfo.second.clear();
		prevCallFunInfo.second.clear();

		currDebugBreakingPointInfo.clear();
		prevDebugBreakingPointInfo.clear();
		holdDebugBreakingPointInfo.clear();

		nlohmann::json a;
		a.push_back("debug exit");
		return a;
	}

	nlohmann::json debugInfo(ContextPtr ctx)
	{
		int kb = lua_gc(L, LUA_GCCOUNT, 0);
		int i = 0;
		while (kb >= 1024 && i < 3) {
			kb = kb / 1024;
			i++;
		}
		std::vector<std::string> suffix = {"KB", "MB", "GB", "TB"};
		std::string mem = std::to_string(kb) + suffix[i];

		char hbuf[16];
		snprintf(hbuf, sizeof(hbuf), "%08x", ctx->handle());
		std::string shbuf(hbuf);

		nlohmann::json a;
		nlohmann::json o;
		o["handle"] = shbuf;
		o["mem"] = mem;
		o["cpu"] = ctx->cpuCost();
		o["recive"] = ctx->messageCount();
		o["pending"] = ctx->mailboxLength();
		o["profile"] = ctx->getProfile();
		o["debug"] = hookCount == kDebugHookCount;
		a.push_back(o);
		return a;
	}
	nlohmann::json debugPing(ContextPtr ctx)
	{
		nlohmann::json a;
		a.push_back("pong");
		return a;
	}

private:
	static void hook(lua_State* L, lua_Debug* ar)
	{
		if (lua_isyieldable(L)) {
			lua_getfield(L, LUA_REGISTRYINDEX, "context");
			Context* ctx = (Context*)lua_touserdata(L, -1);
			Snlua* self = (Snlua*)ctx->entry();
			lua_getinfo(L, "lS", ar);
			if (self->hookCount == kDebugHookCount && self->hasBreakingPoint(ctx, ar)) {	
				self->breakingPoint = 1;
			} else {
				ctx->yield();
			}
			lua_yield(L, 0);
		}
	}

	static const int kNormalHookMask  = LUA_MASKCOUNT;  // 正常调度模式
	static const int kNormalHookCount = 1000;           // 正常调度模式

	static const int kDebugHookMask   = LUA_MASKLINE;   // 调试调度模式
	static const int kDebugHookCount  = 0;              // 调试调度模式
	
	static const int kNext = 0;
	static const int kStep = 1;
private:
	lua_State* L;
	Log log;
	int fcout = 0;
	int hookCount = kNormalHookCount;
	int hookMask  = kNormalHookMask;
	
	int breakingPoint = 0;
	int debugActionType = kNext;
	
	std::pair<std::string, uint32_t> currBreakingPointInfo;
	std::pair<std::string, uint32_t> prevBreakingPointInfo;
	std::pair<std::string, std::set<uint32_t>> currCallFunInfo;
	std::pair<std::string, std::set<uint32_t>> prevCallFunInfo;
	std::list<std::pair<std::string,uint32_t>> currDebugBreakingPointInfo;
	std::list<std::pair<std::string,uint32_t>> prevDebugBreakingPointInfo;
	std::list<std::pair<std::string,uint32_t>> holdDebugBreakingPointInfo;

	uint32_t debugWatcher = 0;
};


module(Snlua)