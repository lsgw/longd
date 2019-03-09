#include "udp.h"
#include "lua/lua.hpp"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <functional>
#include <string>
#include <map>


static int lua_udp_decode_open(lua_State* L)
{
	assert(lua_gettop(L) == 6);
	UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
	bzero(packet, sizeof(UdpPacket));

	packet->type    = UdpPacket::kOpen;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);
	size_t len = 0;
	void*  str = (void *)luaL_checklstring(L, 4, &len);
	memcpy(packet->open.ip, str, len);
	packet->open.port = (uint16_t)luaL_checkinteger(L, 5);
	packet->open.ipv6 = (bool)lua_toboolean(L, 6);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(UdpPacket));

	return 2;
}
static int lua_udp_decode_send(lua_State* L)
{
	uint32_t id      = (uint32_t)luaL_checkinteger(L, 2);
	uint32_t session = (uint32_t)luaL_checkinteger(L, 3);
	char*    ip      = (char*)lua_tostring(L, 4);
	uint16_t port    = (uint16_t)luaL_checkinteger(L, 5);
	bool     ipv6    = (bool)lua_toboolean(L, 6);

	int luatype = lua_type(L, 7);
	if (luatype == LUA_TSTRING) {
		size_t len = 0;
		void* data = (void*)lua_tolstring(L, 7, &len);
		UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket) + len);
		bzero(packet, sizeof(UdpPacket));

		packet->type = UdpPacket::kSend;
		packet->id = id;
		packet->session = session;
		strcpy(packet->send.ip, ip);
		packet->send.port = port;
		packet->send.ipv6 = ipv6;
		packet->send.nbyte = len;
		memcpy(packet->send.data, data, len);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(UdpPacket) + len);

		return 2;
	} else if (luatype == LUA_TLIGHTUSERDATA) {
		void* data = (void*)lua_touserdata(L, 7);
		size_t len = (size_t)luaL_checkinteger(L, 8);
		UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket) + len);
		bzero(packet, sizeof(UdpPacket));

		packet->type = UdpPacket::kSend;
		packet->id = id;
		packet->session = session;
		strcpy(packet->send.ip, ip);
		packet->send.port = port;
		packet->send.ipv6 = ipv6;
		packet->send.nbyte = len;
		memcpy(packet->send.data, data, len);
		free(data);

		lua_pushlightuserdata(L, packet);
		lua_pushinteger(L, sizeof(UdpPacket) + len);

		return 2;
	} else {
		return 0;
	}
}

static int lua_udp_decode_recv(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
	bzero(packet, sizeof(UdpPacket));

	packet->type    = UdpPacket::kRecv;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(UdpPacket));
	return 2;
}
static int lua_udp_decode_close(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
	bzero(packet, sizeof(UdpPacket));

	packet->type    = UdpPacket::kClose;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(UdpPacket));
	return 2;
}
static int lua_udp_decode_opts(lua_State* L)
{
		// printf("lua_cmd_decode_opts\n");
	assert(lua_gettop(L) == 4);
	assert(lua_type(L, 4) == LUA_TTABLE);
	UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
	bzero(packet, sizeof(UdpPacket));

	packet->type = UdpPacket::kOpts;
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
	lua_pushinteger(L, sizeof(UdpPacket));

	return 2;
}

static int lua_udp_decode_info(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	UdpPacket* packet = (UdpPacket*)malloc(sizeof(UdpPacket));
	bzero(packet, sizeof(UdpPacket));

	packet->type    = UdpPacket::kInfo;
	packet->id      = (uint32_t)luaL_checkinteger(L, 2);
	packet->session = (uint32_t)luaL_checkinteger(L, 3);

	lua_pushlightuserdata(L, packet);
	lua_pushinteger(L, sizeof(UdpPacket));
	
	return 2;
}

static std::map<std::string,std::function<int(lua_State*)>> name_to_func = {
	{ "open",  lua_udp_decode_open  },
	{ "send",  lua_udp_decode_send  },
	{ "recv",  lua_udp_decode_recv  },
	{ "close", lua_udp_decode_close },
	{ "opts",  lua_udp_decode_opts  },
	{ "info",  lua_udp_decode_info  }
};

static int lua_udp_encode_to_lightuserdata(lua_State* L)
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















static int lua_udp_decode_from_lightuserdata(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	assert(lua_type(L, 2) == LUA_TNUMBER);
	UdpPacket* packet = (UdpPacket*)lua_touserdata(L, 1);
	assert(packet != NULL);
	lua_pushinteger(L, (uint32_t)packet->type);
	lua_pushinteger(L, (uint32_t)packet->id);
	lua_pushinteger(L, (uint32_t)packet->session);
	
	int retn = 3;
	if (packet->type == UdpPacket::kStatus) {
		lua_pushboolean(L, packet->status.online);
		retn += 1;
	} else if (packet->type == UdpPacket::kRecv) {
		lua_pushlstring(L, packet->recv.data, packet->recv.nbyte);
		lua_newtable(L);

		lua_pushstring(L, "ip");
		lua_pushstring(L, packet->recv.ip);
		lua_settable(L, -3);

		lua_pushstring(L, "port");
		lua_pushinteger(L, packet->recv.port);
		lua_settable(L, -3);

		lua_pushstring(L, "ipv6");
		lua_pushboolean(L, packet->recv.ipv6);
		lua_settable(L, -3);
		retn += 2;
	} else if (packet->type == UdpPacket::kInfo) {
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

		lua_pushstring(L, "writeError");
		lua_pushinteger(L, packet->info.writeError);
		lua_settable(L, -3);

		retn += 1;
	} else {
		assert(false);
		retn = 0;
	}

	return retn;
}


extern "C" int luaopen_api_udp_codec(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "encode_to_lightuserdata",   lua_udp_encode_to_lightuserdata   },
		{ "decode_from_lightuserdata", lua_udp_decode_from_lightuserdata },
		{  NULL,    NULL           }
	};
	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);
	return 1;
}