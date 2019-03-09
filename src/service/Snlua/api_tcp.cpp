#include "tcp.h"
#include "lua/lua.hpp"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <functional>
#include <string>
#include <map>

static int lua_tcp_decode_listen(lua_State* L)
{
	assert(lua_gettop(L) == 6);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kListen;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	size_t len = 0;
	void*  str = (void *)luaL_checklstring(L, 4, &len);
	memcpy(packet->listen.ip, str, len);
	packet->listen.port = (uint16_t)luaL_checkinteger(L, 5);
	packet->listen.ipv6 = (bool)lua_toboolean(L, 6);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_accept(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kAccept;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	
	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}


static int lua_tcp_decode_connect(lua_State* L)
{
	assert(lua_gettop(L) == 6);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kConnect;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	size_t len = 0;
	void*  str = (void *)luaL_checklstring(L, 4, &len);
	memcpy(packet->connect.ip, str, len);
	packet->connect.port = (uint16_t)luaL_checkinteger(L, 5);
	packet->connect.ipv6 = (bool)lua_toboolean(L, 6);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_read(lua_State* L)
{
	assert(lua_gettop(L) == 4);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type       = TcpPacket::kRead;
	packet->id         = (uint32_t)luaL_checkinteger(L, 2);
	packet->session    = (uint32_t)luaL_checkinteger(L, 3);
	packet->read.nbyte = (uint32_t)luaL_checkinteger(L, 4);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_write(lua_State* L)
{
	int id      = (uint32_t)luaL_checkinteger(L, 2);
	int session = (uint32_t)luaL_checkinteger(L, 3);

	int luatype = lua_type(L, 4);
	if (luatype == LUA_TSTRING) {
		size_t len = 0;
		void* data = (void*)lua_tolstring(L, 4, &len);
		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket) + len);
		bzero(packet, sizeof(TcpPacket));

		packet->type    = TcpPacket::kWrite;
		packet->id      = id;
		packet->session = session;
		packet->write.nbyte = len;
		memcpy(packet->write.data, data, len);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(TcpPacket) + len);

		return 2;
	} else if (luatype == LUA_TLIGHTUSERDATA) {
		void* data = (void*)lua_touserdata(L, 4);
		size_t len = (size_t)luaL_checkinteger(L, 5);
		TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket) + len);
		bzero(packet, sizeof(TcpPacket));

		packet->type    = TcpPacket::kWrite;
		packet->id      = id;
		packet->session = session;
		packet->write.nbyte = len;
		memcpy(packet->write.data, data, len);
		free(data);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(TcpPacket) + len);

		return 2;
	} else {
		return 0;
	}
}

static int lua_tcp_decode_shutdown(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kShutdown;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_close(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kClose;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_opts(lua_State* L)
{
	// printf("lua_cmd_decode_opts\n");
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 4) == LUA_TTABLE);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kOpts;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushnil(L);
	while(lua_next(L, -2)) {
		if (lua_type(L, -2) == LUA_TSTRING && (lua_type(L, -1) == LUA_TBOOLEAN || lua_type(L, -1) == LUA_TNUMBER)) {
			size_t nkey = 0;
			const char* key = luaL_checklstring(L, -2, &nkey);

			if (strncmp(key, "reuseaddr", nkey) == 0) {
				packet->opts.optsbits |= 0B00000001;
				packet->opts.reuseaddr = lua_toboolean(L, -1);
			}
			if (strncmp(key, "reuseport", nkey) == 0) {
				packet->opts.optsbits |= 0B00000010;
				packet->opts.reuseport = lua_toboolean(L, -1);
			}
			if (strncmp(key, "keepalive", nkey) == 0) {
				packet->opts.optsbits |= 0B00000100;
				packet->opts.keepalive = lua_toboolean(L, -1);
			}
			if (strncmp(key, "nodelay", nkey) == 0) {
				packet->opts.optsbits |= 0B00001000;
				packet->opts.nodelay   = lua_toboolean(L, -1);
			}
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
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_info(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type    = TcpPacket::kInfo;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}

static int lua_tcp_decode_low_water_mark(lua_State* L)
{
	assert(lua_gettop(L) == 5);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));
	packet->type    = TcpPacket::kLowWaterMark;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	packet->lowWaterMark.on = (bool)lua_toboolean(L, 4);
	packet->lowWaterMark.value = (uint64_t)luaL_checkinteger(L, 5);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}
static int lua_tcp_decode_high_water_mark(lua_State* L)
{
	assert(lua_gettop(L) == 5);
	TcpPacket* packet = (TcpPacket*)malloc(sizeof(TcpPacket));
	bzero(packet, sizeof(TcpPacket));

	packet->type = TcpPacket::kHighWaterMark;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	packet->highWaterMark.on = (bool)lua_toboolean(L, 4);
	packet->highWaterMark.value = (uint64_t)luaL_checkinteger(L, 5);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(TcpPacket));

	return 2;
}


static std::map<std::string,std::function<int(lua_State*)>> name_to_func = {
	{ "listen",   lua_tcp_decode_listen   },
	{ "accept",   lua_tcp_decode_accept   },
	{ "connect",  lua_tcp_decode_connect  },
	{ "read",     lua_tcp_decode_read     },
	{ "write",    lua_tcp_decode_write    },
	{ "shutdown", lua_tcp_decode_shutdown },
	{ "close",    lua_tcp_decode_close    },
	{ "opts",     lua_tcp_decode_opts     },
	{ "info",     lua_tcp_decode_info     },

	{ "low_water_mark",  lua_tcp_decode_low_water_mark  },
	{ "high_water_mark", lua_tcp_decode_high_water_mark }
};

static int lua_tcp_encode_to_lightuserdata(lua_State* L)
{
	assert(lua_type(L, 1) == LUA_TSTRING);
	std::string name = lua_tostring(L, 1);
	if (name_to_func.find(name) != name_to_func.end()) {
		auto encode = name_to_func[name];
		return encode(L);
	} else {
		assert(false);
		return 0;
	}
}







static int lua_tcp_decode_from_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	assert(lua_type(L, 2) == LUA_TNUMBER);
	TcpPacket* packet = (TcpPacket*)lua_touserdata(L, 1);
	uint32_t size = (uint32_t)luaL_checkinteger(L, 2);
	(void)size;
	assert(packet != NULL);
	lua_pushinteger(L, (uint32_t)packet->type);
	lua_pushinteger(L, (uint32_t)packet->id);
	lua_pushinteger(L, (uint32_t)packet->session);

	int retn = 3;
	if (packet->type == TcpPacket::kStatus) {
		lua_pushboolean(L, packet->status.online);
		lua_pushinteger(L, packet->status.id);
		retn += 2;
	} else if (packet->type == TcpPacket::kAccept) {
		lua_pushinteger(L, packet->accept.id);

		lua_newtable(L);

		lua_pushstring(L, "ip");
		lua_pushstring(L, packet->accept.ip);
		lua_settable(L, -3);

		lua_pushstring(L, "port");
		lua_pushinteger(L, packet->accept.port);
		lua_settable(L, -3);

		lua_pushstring(L, "ipv6");
		lua_pushboolean(L, packet->accept.ipv6);
		lua_settable(L, -3);
		retn += 2;
	} else if (packet->type == TcpPacket::kRead) {
		lua_pushlstring(L, packet->read.data, packet->read.nbyte);
		retn += 1;
	} else if (packet->type == TcpPacket::kLowWaterMark) {
		lua_pushinteger(L, packet->lowWaterMark.value);
		retn += 1;
	} else if (packet->type == TcpPacket::kHighWaterMark) {
		lua_pushinteger(L, packet->highWaterMark.value);
		retn += 1;
	} else if (packet->type == TcpPacket::kInfo) {
		lua_newtable(L);
		lua_pushstring(L, "addr");
		lua_newtable(L);

		lua_pushstring(L, "ip");
		lua_pushstring(L, packet->info.ip);
		lua_settable(L, -3);

		lua_pushstring(L, "port");
		lua_pushinteger(L, packet->info.port);
		lua_settable(L, -3);

		lua_pushstring(L, "ipv6");
		lua_pushboolean(L, packet->info.ipv6);
		lua_settable(L, -3);

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

extern "C" int luaopen_api_tcp_codec(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "encode_to_lightuserdata",   lua_tcp_encode_to_lightuserdata   },
		{ "decode_from_lightuserdata", lua_tcp_decode_from_lightuserdata },
		{  NULL,    NULL           }
	};
	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);
	return 1;
}