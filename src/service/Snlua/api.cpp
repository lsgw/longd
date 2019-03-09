#include "Actor.h"
#include "Types.h"
#include "lua/lua.hpp"
#include <string>
#include <vector>
#include <map>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>


static int lua_newsession(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t s = ctx->newsession();
	lua_pushinteger(L, s);
	return 1;
}

static int lua_send(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 3) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	
	uint32_t handle  = (uint32_t)luaL_checkinteger(L, 1);
	uint32_t type    = (uint32_t)luaL_checkinteger(L, 2);
	void*    data    = (void*)lua_touserdata(L, 3);
	uint32_t size    = (uint32_t)luaL_checkinteger(L, 4);

	auto msg     = ctx->makeMessage();
	msg->type    = type;
	msg->data    = data;
	msg->size    = size;

	bool b = ctx->send(handle, msg);
	// printf("ub = %d, handle=%d\n", b, handle);
	if (b) {
		lua_pushboolean(L, 1);
		lua_pushnil(L);
	} else {
		std::string err = "send to invalid address: " + std::to_string(handle);
		lua_pushboolean(L, 0);
		lua_pushlstring(L, err.data(), err.size());
	}
	return 2;
}

static int lua_timeout(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	if (lua_gettop(L) != 2) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "param need: uint32_t session, double second");
		return 2;
	}

	if (lua_type(L, -2) != LUA_TNUMBER) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "first param must be an Number");
    	return 2;
    }
    if (lua_type(L, -1) != LUA_TNUMBER) {
    	lua_pushboolean(L, 0);
		lua_pushstring(L, "second param must be an Number");
    	return 2;
    }
	double second  = lua_tonumber(L, 1);
	int session = lua_tointeger(L, 2);

   	ctx->timeout(second, session);

   	lua_pushboolean(L, 1);
   	lua_pushinteger(L, session);
	
	return 2;
}
static int lua_timeout_decode_from_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	assert(lua_type(L, 2) == LUA_TNUMBER);
	uint32_t* data = (uint32_t*)lua_touserdata(L, 1);
	assert(data != NULL);
	uint32_t size = (uint32_t)luaL_checkinteger(L, 2);
	assert(size == sizeof(uint32_t));

	lua_pushinteger(L, *data);

	return 1;
}


static int lua_handle(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->handle());
	return 1;
}
static int lua_recv(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t mid = (uint32_t)luaL_checkinteger(L, 1);
	MessagePtr message = ctx->get(mid);
	assert(message.get() != NULL);
	lua_pushinteger(L,       message->source);
	lua_pushinteger(L,       message->type);
	lua_pushlightuserdata(L, message->data);
	lua_pushinteger(L,       message->size);
	return 4;
}
static int lua_free(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t mid = (uint32_t)luaL_checkinteger(L, 1);
	assert(ctx->free(mid));
	return 0;
}

static int lua_exit(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	ctx->exit();
	return 0;
}

static int lua_abort(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	ctx->abort();
	return 0;
}

static int lua_newport(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 3) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	std::string driver = lua_tostring(L, 1);
	uint32_t type      = (uint32_t)luaL_checkinteger(L, 2);
	void*    data      = (void*)lua_touserdata(L, 3);
	uint32_t size      = (uint32_t)luaL_checkinteger(L, 4);

	uint32_t id = ctx->newport(driver, type, data, size);

	if (id > 0) {
		lua_pushinteger(L, id);
		lua_pushnil(L);
	} else {
		std::string err = "open driver [" + driver + "] fail!";
		lua_pushnil(L);
		lua_pushlstring(L, err.data(), err.size());
	}

    return 2;
}

static int lua_command(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 3) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	uint32_t id      = (uint32_t)luaL_checkinteger(L, 1);
	uint32_t type    = (uint32_t)luaL_checkinteger(L, 2);
	void*    data    = (void*)lua_touserdata(L, 3);
	uint32_t size    = (uint32_t)luaL_checkinteger(L, 4);

	auto msg     = ctx->makeMessage();
	msg->type    = type;
	msg->data    = data;
	msg->size    = size;

	bool b = ctx->command(id, msg);
	if (b) {
		lua_pushboolean(L, 1);
		lua_pushnil(L);
	} else {
		std::string err = "command to invalid address: " + std::to_string(id);
		lua_pushboolean(L, 0);
		lua_pushlstring(L, err.data(), err.size());
	}
	
	return 2;
}


static int lua_log(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	lua_pushinteger(L, ctx->env().loghandle);
	return 1;
}
static int lua_sig(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	lua_pushinteger(L, ctx->env().sigid);
	return 1;
}
static int lua_cpu(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	lua_pushinteger(L, ctx->cpuCost());
	return 1;
}
static int lua_psize(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	lua_pushinteger(L, ctx->mailboxLength());
	return 1;
}
static int lua_msize(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	lua_pushinteger(L, ctx->messageCount());
	return 1;
}
static int lua_set_profile(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	bool on = (bool)lua_toboolean(L, 1);
	ctx->setProfile(on);
	return 0;
}
static int lua_get_profile(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	bool on = ctx->getProfile();
	lua_pushboolean(L, on);
	return 1;
}

static int lua_port_list(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	auto m = ctx->portList();
	lua_newtable(L);
	for (auto p : m) {
		lua_pushinteger(L, p.first);
		lua_pushinteger(L, p.second);
		lua_settable(L, -3);
	}
	return 1;
}

static int lua_port_name(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
	std::string name = ctx->portname(id);
	lua_pushlstring(L, name.data(), name.size());
	return 1;
}


static int lua_now(lua_State* L)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
	lua_pushinteger(L, cp);
	return 1;
}

extern "C" int luaopen_api(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "newsession",  lua_newsession  },
		{ "send",        lua_send        },
		{ "timeout",     lua_timeout     },
		{ "timeout_decode_from_lightuserdata", lua_timeout_decode_from_lightuserdata },
		{ "handle",      lua_handle      },
		{ "recv",        lua_recv        },
		{ "free",        lua_free        },
		{ "exit",        lua_exit        },
		{ "abort",       lua_abort       },

		{ "newport",     lua_newport     },
		{ "command",     lua_command     },
		{ "log",         lua_log         },
		{ "sig",         lua_sig         },

		{ "cpu",         lua_cpu         },
		{ "psize",       lua_psize       },
		{ "msize",       lua_msize       },
		{ "set_profile", lua_set_profile },
		{ "get_profile", lua_get_profile },
		{ "port_list",   lua_port_list   },
		{ "port_name",   lua_port_name   },
		{ "now",         lua_now         },
		{  NULL,         NULL            }
	};
	luaL_newlibtable(L, funcs);

	lua_getfield(L, LUA_REGISTRYINDEX, "context");
	Context* ctx = (Context*)lua_touserdata(L,-1);
	
	if (ctx == NULL) {
		return luaL_error(L, "Init context first");
	}

	luaL_setfuncs(L, funcs, 1);

	return 1;
}