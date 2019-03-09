#include "exe.h"
#include "lua/lua.hpp"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <functional>
#include <string>
#include <map>

static int lua_exe_decode_open(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	size_t len = 0;
	void*  str = (void *)luaL_checklstring(L, 4, &len);
	ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + len);

	packet->type    = ExePacket::kOpen;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	packet->open.len = len;
	memcpy(packet->open.path, str, len);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(ExePacket) + len);

	return 2;
}
static int lua_exe_decode_write(lua_State* L)
{
	uint32_t id      = (uint32_t)luaL_checkinteger(L, 2);
	uint32_t session = (uint32_t)luaL_checkinteger(L, 3);

	int luatype = lua_type(L, 4);
	if (luatype == LUA_TSTRING) {
		size_t len = 0;
		void* data = (void*)lua_tolstring(L, 4, &len);
		ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + len);
		bzero(packet, sizeof(ExePacket));

		packet->type = ExePacket::kWrite;
		packet->id = id;
		packet->session = session;
		packet->write.nbyte = len;
		memcpy(packet->write.data, data, len);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(ExePacket) + len);

		return 2;
	} else if (luatype == LUA_TLIGHTUSERDATA) {
		void* data = (void*)lua_touserdata(L, 4);
		size_t len = (size_t)luaL_checkinteger(L, 5);
		ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket) + len);
		bzero(packet, sizeof(ExePacket));

		packet->type = ExePacket::kWrite;
		packet->id = id;
		packet->session = session;
		packet->write.nbyte = len;
		memcpy(packet->write.data, data, len);
		free(data);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(ExePacket) + len);

		return 2;
	} else {
		return 0;
	}
}
static int lua_exe_decode_read(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
	bzero(packet, sizeof(ExePacket));

	packet->type    = ExePacket::kRead;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	packet->read.nbyte = (uint32_t)luaL_checkinteger(L, 4);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(ExePacket));

	return 2;
}
static int lua_exe_decode_close(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
	bzero(packet, sizeof(ExePacket));

	packet->type = ExePacket::kClose;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(ExePacket));
	return 2;
}
static int lua_exe_decode_opts(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 4) == LUA_TTABLE);
	ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
	bzero(packet, sizeof(ExePacket));

	packet->type = ExePacket::kOpts;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushnil(L);
	while(lua_next(L, -2)) {
		if (lua_type(L, -2) == LUA_TSTRING && (lua_type(L, -1) == LUA_TBOOLEAN || lua_type(L, -1) == LUA_TNUMBER)) {
			size_t nkey = 0;
			const char* key = luaL_checklstring(L, -2, &nkey);

			if (strncmp(key, "active", nkey) == 0) {
				packet->opts.optsbits |= 0B00010000;
				packet->opts.active    = lua_toboolean(L, -1);
			}
			if (strncmp(key, "owner", nkey) == 0) {
				packet->opts.optsbits |= 0B00100000;
				packet->opts.owner     = lua_tointeger(L, -1);
			}
			if (strncmp(key, "read", nkey) == 0) {
				packet->opts.optsbits |= 0B01000000;
				packet->opts.read      = lua_toboolean(L, -1);
			}
		}
		lua_pop(L, 1);
	}


	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(ExePacket));

	return 2;
}

static int lua_exe_decode_info(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	ExePacket* packet = (ExePacket*)malloc(sizeof(ExePacket));
	bzero(packet, sizeof(ExePacket));

	packet->type    = ExePacket::kInfo;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(ExePacket));
	return 2;
}

static std::map<std::string,std::function<int(lua_State*)>> name_to_func = {
	{ "open",  lua_exe_decode_open  },
	{ "write", lua_exe_decode_write },
	{ "read",  lua_exe_decode_read  },
	{ "close", lua_exe_decode_close },
	{ "opts",  lua_exe_decode_opts  },
	{ "info",  lua_exe_decode_info  }
};

static int lua_exe_encode_to_lightuserdata(lua_State* L)
{
	assert(lua_type(L, 1) == LUA_TSTRING);
	std::string name = lua_tostring(L, 1);
	if (name_to_func.find(name) != name_to_func.end()) {
		auto encode = name_to_func[name];
		return encode(L);
	} else {
		abort();
		return 0;
	}
}



static int lua_exe_decode_from_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	assert(lua_type(L, 2) == LUA_TNUMBER);
	ExePacket* packet = (ExePacket*)lua_touserdata(L, 1);
	assert(packet != NULL);
	lua_pushinteger(L, (uint32_t)packet->type);
	lua_pushinteger(L, (uint32_t)packet->id);
	lua_pushinteger(L, (uint32_t)packet->session);

	int retn = 3;
	if (packet->type == ExePacket::kStatus) {
		lua_pushboolean(L, packet->status.online);
		lua_pushinteger(L, packet->status.pid);
		retn += 2;
	} else if (packet->type == ExePacket::kRead) {
		lua_pushlstring(L, packet->read.data, packet->read.nbyte);
		retn += 1;
	} else if (packet->type == ExePacket::kInfo) {
		lua_newtable(L);

		lua_pushstring(L, "cmdline");
		lua_pushlstring(L, packet->info.data, packet->info.nbyte);
		lua_settable(L, -3);

		lua_pushstring(L, "owner");
		lua_pushinteger(L, packet->info.owner);
		lua_settable(L, -3);

		lua_pushstring(L, "start");
		lua_pushstring(L, packet->info.start);
		lua_settable(L, -3);

		lua_pushstring(L, "readCount");
		lua_pushinteger(L, packet->info.readCount);
		lua_settable(L, -3);

		lua_pushstring(L, "readBuff");
		lua_pushinteger(L, packet->info.readBuff);
		lua_settable(L, -3);

		lua_pushstring(L, "writeCount");
		lua_pushinteger(L, packet->info.writeCount);
		lua_settable(L, -3);

		lua_pushstring(L, "writeBuff");
		lua_pushinteger(L, packet->info.writeBuff);
		lua_settable(L, -3);

		retn += 1;
	} else {
		assert(false);
		retn = 0;
	}

	return retn;
}

extern "C" int luaopen_api_exe_codec(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "encode_to_lightuserdata",   lua_exe_encode_to_lightuserdata   },
		{ "decode_from_lightuserdata", lua_exe_decode_from_lightuserdata },
		{  NULL,    NULL           }
	};

	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);

	return 1;
}