#include "sig.h"
#include "lua/lua.hpp"
#include <assert.h>
#include <string.h>
#include <string>

static int lua_sig_encode_to_lightuserdata(lua_State* L)
{
	assert(lua_type(L, 1) == LUA_TSTRING);
	std::string op = lua_tostring(L, 1);
	if (op == "action") {
		assert(lua_gettop(L) == 3);
		SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
		packet->type = SigPacket::kActionModule;
		packet->actionModule.handle = (uint32_t)luaL_checkinteger(L, 2);
		packet->actionModule.sig    = (uint32_t)luaL_checkinteger(L, 3);
		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(SigPacket));
		return 2;
	} else if (op == "cancel") {
		assert(lua_gettop(L) == 3);
		SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
		packet->type = SigPacket::kCancelModule;
		packet->cancelModule.handle = (uint32_t)luaL_checkinteger(L, 2);
		packet->cancelModule.sig    = (uint32_t)luaL_checkinteger(L, 3);
		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(SigPacket));
		return 2;
	} else if (op == "info") {
		assert(lua_gettop(L) == 1);
		SigPacket* packet = (SigPacket*)malloc(sizeof(SigPacket));
		packet->type = SigPacket::kInfo;
		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(SigPacket));
		return 2;
	} else {
		assert(false);
		return 0;
	}
}

static int lua_sig_decode_from_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	assert(lua_type(L, 2) == LUA_TNUMBER);
	SigPacket* packet = (SigPacket*)lua_touserdata(L, 1);
	assert(packet != NULL);
	lua_pushinteger(L, (uint32_t)packet->type);

	int retn = 0;
	if (packet->type == SigPacket::kHappen) {
		lua_pushinteger(L, packet->happen.sig);
		retn = 2;
	} else if (packet->type == SigPacket::kInfo) {
		lua_pushlstring(L, packet->info.data, packet->info.nbyte);
		retn = 2;
	} else {
		assert(false);
		retn = 0;
	}
	return retn;
}

static int lua_sig_platform(lua_State* L)
{
#ifdef __MACH__
	lua_pushstring(L, "macosx");
#else
	lua_pushstring(L, "linux");
#endif
	return 1;
}

extern "C" int luaopen_api_sig_codec(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "encode_to_lightuserdata",   lua_sig_encode_to_lightuserdata   },
		{ "decode_from_lightuserdata", lua_sig_decode_from_lightuserdata },
		{ "platform",                  lua_sig_platform                  },
		{  NULL,  NULL  }
	};

	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);

	return 1;
}