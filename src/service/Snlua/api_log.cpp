#include "lua/lua.hpp"
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static int lua_log_encode_to_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 2) == LUA_TSTRING);

	uint32_t level = (uint32_t)luaL_checkinteger(L, 1);
	size_t len  = 0;
	char*  str  = (char*)lua_tolstring(L, 2, &len);
	int    size = sizeof(uint32_t) + len;
	char*  data = (char*)malloc(size);
	*(uint32_t*)data = level;
	memcpy(data+sizeof(uint32_t), str, len);

	lua_pushlightuserdata(L, data);
	lua_pushinteger(L, size);

	return 2;
}

extern "C" int luaopen_api_log_codec(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "encode_to_lightuserdata", lua_log_encode_to_lightuserdata },
		{  NULL,  NULL  }
	};

	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);

	return 1;
}