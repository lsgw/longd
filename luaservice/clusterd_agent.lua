local system = require "system"
local logger = require "logger"
local tcp    = require "tcp"
local json   = require "rapidjson"
local id, clusterd, localaddr = ...
local peeraddr
local codec = { }
local CMD   = { }
local cache = ""


function codec.pack(c, ...)
	local j = json.encode({c, ...})
	local s = string.pack(">I4s4", c, j)
	return s
end
function codec.unpack(s)
	local c, d, left = string.unpack(">I4s4", s)
	local j = json.decode(d)
	local t = table.remove(j, 1);
	assert(c == t)
	return c, j, string.sub(s, left)
end



function CMD.query(name)
	return {pcall(system.query, name)}
end
function CMD.call(...)
	return {pcall(system.call, ...)}
end
function CMD.cast(...)
	return {pcall(system.cast, ...)}
end
function CMD.remote(_peeraddr, peername, peerhide)
	peeraddr = _peeraddr
	return {pcall(system.cast, clusterd, "lua", "remote", system.self(), peeraddr, peername, peerhide)}
end
function CMD.heartbeat(peernodes)
	return {pcall(system.cast, clusterd, "lua", "heartbeat", system.self(), peeraddr, peernodes)}
end

local function docmd(f, ...)
	logger.info("clusterd_agent message", peeraddr, f, ...)
	local fun = CMD[f]
	assert(fun, "not find function " .. f)
	local rets = fun(...)
	local ok = table.remove(rets, 1)
	if ok then
		return rets
	else
		logger.info("clusterd_agent message", peeraddr, f, table.unpack({...}), table.unpack(rets))
		return { }
	end
end

-- logger.info("new clusterd_agent id->",id)
tcp.start(id, {active=true})

while true do
	system.receive {
		[{"tcp"}] = function(type, subtype, id, session, msg)
			if #msg == 0 then
				return
			end
			cache = cache .. msg
			while true do
				local ok, c, j, remain = pcall(codec.unpack, cache)
				if not ok then
					break
				end
				cache = remain

				local rets = docmd(table.unpack(j))
				if c > 0 then
					tcp.write(id, codec.pack(c, table.unpack(rets)))
					logger.info("c", c)
				end
			end
		end
	}
end
