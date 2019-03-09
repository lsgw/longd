local api   = require "api"
local json  = require "rapidjson"
local seri  = require "serialize"
local trace = require "trace"

local system = { protocol = {
	MSG_TYPE_EXIT     =  1,
	MSG_TYPE_TIME     =  2,
	MSG_TYPE_EVENT    =  3,
	MSG_TYPE_JSON     =  4,
	MSG_TYPE_LUA      =  5,
	MSG_TYPE_LOG      =  6,
	
	MSG_TYPE_TCP      =  7,
	MSG_TYPE_UDP      =  8,
	MSG_TYPE_SIG      =  9,
	MSG_TYPE_EXE      = 10,

	MSG_TYPE_TRACE    = 11,
	MSG_TYPE_DEBUG    = 12,
}}

function system.protocol.register(class)
 	local name = class.name
	local id = class.id
	assert(system.protocol[name] == nil and system.protocol[id] == nil)
	assert(type(name) == "string" and type(id) == "number" and id >=1 and id <=255)
	system.protocol[name] = class
	system.protocol[id] = class
end

do
	system.protocol.register({
		name     = "json",
		id       = system.protocol.MSG_TYPE_JSON,
		pack     = function(...) return json.encode_to_lightuserdata({...}) end,
		unpack   = function(...) return table.unpack(json.decode_from_lightuserdata(...)) end,
	})
	system.protocol.register({
		name     = "lua",
		id       = system.protocol.MSG_TYPE_LUA,
		pack     = seri.encode_to_lightuserdata,
		unpack   = seri.decode_from_lightuserdata,
	})
	system.protocol.register({
        name     = "time",
        id       = system.protocol.MSG_TYPE_TIME,
        unpack   = function (...) return api.timeout_decode_from_lightuserdata(...) end,
        dispatch = function(type, session)
        end
    })
	system.protocol.register({
		name     = "trace",
		id       = system.protocol.MSG_TYPE_TRACE,
		pack     = function(...) return json.encode_to_lightuserdata({...}) end,
		unpack   = function(...) return table.unpack(json.decode_from_lightuserdata(...)) end,
		dispatch = function(type, pattern, ref, source, f, ...)
			local rets = { }
			if trace[f] then
				rets = {trace[f](...)}
			end
			if pattern=="call" then
				system.send(source, type, "resp", ref, system.self(), table.unpack(rets))
			end
		end
	})
end

local function format(t)
	local m = { }
	for key, fun in pairs(t) do
		assert(type(key)=="table" and type(fun)=="function")
		if #key>0 and type(key[1])=="string" and key[1]=="after" then
			assert(type(key[2])=="number")
			local succ, session = api.timeout(key[2], api.newsession())
    		assert(succ, tostring(session))
			local typeid = system.protocol["time"].id
			m[{typeid, session}] = fun
		elseif #key>0 and type(key[1])=="string" then
			local typeid = system.protocol[key[1]].id
			m[{typeid, table.unpack(key, 2)}] = fun
		else
			m[key] = fun
		end
	end
	return m
end

local function equals(wait, recv)
	assert(type(wait) == "table")
    for i, _ in pairs(wait) do
        if wait[i] ~= recv[i] then
			return false
        end
    end
    return true
end
local function kpairs(t)
	local a = {}
	for n in pairs(t) do
		a[#a + 1] = n
	end
	table.sort(a, function(l, r) return #l > #r end)
	local i = 0
	return function()
		i = i + 1
		return a[i], t[a[i]]
	end
end
local function dispatch(type, ...)
	local p = system.protocol[type]
	if p and p.dispatch then
		p.dispatch(type, ...)
		return true
	else
		return false
	end
end

function system.receive(t)
	local m = format(t)
    local f = true
    local s = 0
    while true do
        s = coroutine.yield(f, s)
        local recv_source, recv_type, recv_udata, recv_nbyte = api.recv(s)
        local recv = {recv_type, system.protocol[recv_type].unpack(recv_udata, recv_nbyte)}
        for wait, fun in kpairs(m) do
            if equals(wait, recv) then
                f = true
                api.free(s)
				return fun(table.unpack(recv))
            end
        end
        if dispatch(table.unpack(recv)) then
            f = true
            api.free(s)
        else
        	f = false
        end
    end
end

function system.self()
    return api.handle()
end
function system.now()
	local time = { }
	function time.second()
		return api.now() / 1000 / 1000
	end
	function time.millis()
		return api.now() / 1000
	end
	function time.micros()
		return api.now()
	end
	return time
end
function system.port_list()
	return api.port_list()
end
function system.port_name(id)
	return api.port_name(id)
end

function system.ref()
	return tostring(api.handle()) .. "-" .. tostring(api.newsession()) .. "-" .. tostring(api.now())
end
function system.send(handle, type, ...)
	assert(system.protocol[type], "can't find "..type.." protocol")
	assert(handle~=api.handle(), "can't call self")
	local p = system.protocol[type]
    local udata, nbyte = p.pack(...)
	return api.send(handle, p.id, udata, nbyte)
end
function system.call(handle, type, ...)
	local ref = system.ref()
    local succ, err = system.send(handle, type, "call", ref, system.self(), ...)
	assert(succ, tostring(err))
	return system.receive {
		[{type, "resp", ref, handle}] = function(type, response, ref, source, ...)
			return ...
		end
	}
end
function system.cast(handle, type, ...)
	local ref = "ref"
	local succ, err = system.send(handle, type, "cast", ref, system.self(), ...)
	assert(succ, tostring(err))
end


function system.register(handle, name)
	assert(type(handle)=="number" and type(name) == "string", "param need : uint32_t handle, string name")
    local succ, err = system.call(1, "json", "register", handle, name)
    assert(succ, "register error : " .. tostring(err))
end

function system.query(name)
	assert(type(name)=="string", "service name must be an string")
	return system.call(1, "json", "query", name)
end
function system.kill(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.cast(1, "json", "kill", handle)
end

function system.newservice(...)
	return system.call(1, "json", "launch", "Snlua", ...)
end

function system.uniqueservice(name, ...)
	assert(type(name)=="string", "service name must be an string")
	local handle = system.call(1, "json", "query", name)
	if type(handle) == "number" then
		return handle
	else
		handle = system.call(1, "json", "launch", "Snlua", name, ...)
	end
	assert(type(handle) == "number", "new service fail : " .. name)
	system.cast(1, "json", "register", handle, name)
	return handle
end

function system.sleep(second)
	assert(type(second) == "number")
	system.receive({ [{"after", second}] = function(...) end })
end

function system.exit()
	api.exit()
	local s = 0
	while true do
		s = coroutine.yield(true, s)
		api.free(s)
	end
end
function system.abort()
	api.abort()
	local s = 0
	while true do
		s = coroutine.yield(true, s)
		api.free(s)
	end
end

return system