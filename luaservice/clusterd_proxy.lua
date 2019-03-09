local system = require "system"
local logger = require "logger"
local tcp    = require "tcp"
local json   = require "rapidjson"
local id, clusterd, localaddr, localname, localhide, peeraddr = ...

local second = 0.1
local count  = 0
local codec  = { }
local CMD    = { }
local cache  = ""

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

local function next_count()
	count = count + 1
	if count == 0 then
		count = 1
	end
	return count
end

local function request(s)
	logger.info("count", count)
	local r
	tcp.write(id, s)
	while true do
		local msg = tcp.read_timeout(id, second)
		if not msg then
			r = {nil, 1, "request timeout"}
			break
		end
		if #msg == 0 then
			r = {nil, 2, "peernode shutdown"}
			break
		end
		cache = cache .. msg
		local ok, c, j, remain = pcall(codec.unpack, cache)
		if ok then
			if c ~= count then
				r = {nil, 3, "count error"}
			elseif #j == 0 then
				r = {nil, 4, "no value"}
			else
				r = j
			end
			cache = remain
			break
		end
	end
	return table.unpack(r)
end

function CMD.query(name)
	local s = codec.pack(next_count(), "query", name)
	return request(s)
end
function CMD.call(...)
	local s = codec.pack(next_count(), "call", ...)
	return request(s)
end
function CMD.cast(...)
	local s = codec.pack(0, "cast", ...)
	tcp.write(id, s)
end
function CMD.heartbeat(nodes)
	local s = codec.pack(0, "heartbeat", nodes)
	tcp.write(id, s)
end
function CMD.timeout(s)
	if type(s) == "number" and s > 0 and s < 600 then
		second = s
	end
end


-- logger.info("new clusterd_proxy id->",id)
tcp.start(id, {active=false})
tcp.write(id, codec.pack(0, "remote", localaddr, localname, localhide))

while true do
	system.receive {
		[{"lua", "call"}] = function(type, pattern, ref, source, f, ...)
			logger.info("clusterd_proxy command", peeraddr, f,  ...)
			local func = CMD[f]
			local rets = {nil, 10, "no find func " .. f}
			if func then
				rets = {func(...)}
			end
			system.send(source, type, "resp", ref, system.self(), table.unpack(rets))
		end,
		[{"lua", "cast"}] = function(type, pattern, ref, source, f, ...)
			logger.info("clusterd_proxy command", peeraddr, f,  ...)
			local func = CMD[f]
			if func then
				func(...)
			end
		end,
	}
end