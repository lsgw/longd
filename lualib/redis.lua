local tcp   = require "tcp"
local redis = { protocol = { } }


redis.protocol[43] = function(data) -- '+'
	local a, b = data:find("%+%a+\r\n", 1, false)
	if a and a == 1 then
		return true, data:sub(a+1, b-2), data:sub(b+1)
	else
		return false, nil, data
	end
end

redis.protocol[45] = function(data) -- '-'
	local a, b = data:find("%-.+\r\n", 1, false)
	if a and a == 1 then
		return true, data:sub(a+1, b-2), data:sub(b+1)
	else
		return false, nil, data
	end
end

redis.protocol[58] = function(data) -- ':'
	local a, b = data:find(":%d+\r\n", 1, false)
	if a and a == 1 then
		return true, tonumber(data:sub(a+1, b-2)), data:sub(b+1)
	else
		return false, nil, data
	end
end

redis.protocol[36] = function(data) -- '$'
	local a1, b1 = data:find("%$0\r\n\r\n", 1, false)
	if a1 and a1 == 1 then
		return true, "", data:sub(b1+1)
	end
	local a2, b2 = data:find("%$%-1\r\n", 1, false)
	if a2 and a2 == 1 then
		return true, nil, data:sub(b2+1)
	end
	local len = data:match("%$(%d+)\r\n", 1, false)
	if not len then
		return false, nil, data
	end
	local n = tonumber(len)
	if string.len(data) < 1 + string.len(len) + 2 + n + 2 then
		return false, nil, data
	end
	local resp = data:sub(1+string.len(len)+2+1, 1+string.len(len)+2+n)
	local remain = data:sub(1+string.len(len)+2+n+3)
	return true, resp, remain
end

redis.protocol[42] = function(data)	-- '*'
	local a1, b1 = data:find("%*0\r\n", 1, false)
	if a1 and a1 == 1 then
		return true, {}, data:sub(b1+1)
	end
	local a2, b2 = data:find("%*%-1\r\n", 1, false)
	if a2 and a2 == 1 then
		return true, nil, data:sub(b2+1)
	end

	local a3, b3 = data:find("%*%d+\r\n", 1, false)
	if not a3 then
		return false, nil, data
	end
	local m  = data:sub(a3,b3)
	local u = m:match("%*(%d+)\r\n") 
	local n = tonumber(u)
	
	local bulk = {}
	local noerr = true
	local ok, resp, remain
	remain = data:sub(b3+1)
	for i=1, n do
		if string.len(remain) <= 0 then
			noerr = false
			break
		end
		local firstchar = string.byte(remain)
		ok, resp, remain = redis.protocol[firstchar](remain)
		if ok then
			bulk[i] = resp
		else
			noerr = false
			break
		end
	end
	if noerr then
		return true, bulk, remain
	else
		return false, nil, data
	end
end


function redis.pack(...)
	local param = {...}
	local cmd = "*" .. tostring(#param) .. "\r\n"
	for i, value in ipairs(param) do
		local s = tostring(value)
		cmd = cmd .. "$" .. string.len(s) .. "\r\n" .. s .. "\r\n"
	end
	return cmd
end

function redis.unpack(cache)
	assert(string.len(cache) > 0)
	local firstchar = string.byte(cache)
	local ok, resp, remain = redis.protocol[firstchar](cache)
	if ok then
		return true, resp, remain
	else
		return false, nil, cache
	end
end



local _M = setmetatable({ _VERSION = '5.34' }, { __index = function(t, k)
    local cmd = string.upper(k)
    local f = function(self, ...)
    	assert(not self.pubsub, "unable to use this command reason: in subscribe")
		local req = redis.pack(cmd, ...)
		tcp.write(self.conn, req)
		while true do
			local msg = tcp.read(self.conn)
			--print("msg\n", msg)
			if #msg == 0 then
				return nil, "redis connection reset by peer"
			end
			self.cache = self.cache .. msg
		
			local ok, resp, remain = redis.unpack(self.cache)
			if ok then
				self.cache = remain
				return resp
			end
		end
    end
    t[k] = f
    return f
end})

function redis.connect(opts)
	local self = setmetatable({}, { __index = _M })
	local ip   = opts.ip or opts.host
	local port = opts.port or 6379
	local conn, addr = tcp.connect({ip=ip, port=port})
	assert(conn>0)
	tcp.start(conn, {active=false, keepalive=true, nodelay=true})
	self.topic  = { }
	self.conn   = conn
	self.addr   = addr
	self.cache  = ""
	self.pubsub = false
	self.active = false
	return self
end


function _M.id(self)
	return self.conn
end
function _M.opts(self, tab)
	if tab.active ~= nil then
		assert(type(tab.active) == "boolean")
		tcp.setopts(self.conn, {active=tab.active})
		self.active = tab.active
	end
end
function _M.pack(...)
	return redis.pack(...)
end
function _M.unpack(self, msg)
	self.cache = self.cache .. msg
	local ok, resp, remain = redis.unpack(self.cache)
	if ok then
		self.cache = remain
		return table.unpack(resp)
	else
		return nil
	end
end

function _M.close(self)
	tcp.close(self.conn)
	self.topic  = nil
	self.conn   = nil
	self.addr   = nil
	self.cache  = nil
	self.pubsub = nil
	setmetatable(self, nil)
end


function _M.pipeline(self, ...)
	assert(not self.pubsub, "unable to use this command reason: in subscribe")
	local cmds = ""
	local nums = 0
	for i, v in pairs({...}) do
		assert(type(v) == "table")
		cmds = cmds .. redis.pack(table.unpack(v))
		nums = nums + 1
	end
	tcp.write(self.conn, cmds)
	local result = { }
	local i = 0
	while i < nums do
		local msg = tcp.read(self.conn)
		if #msg == 0 then
			return nil, "redis connection reset by peer"
		end
		self.cache = self.cache .. msg
		while true do
			local ok, resp, remain = redis.unpack(self.cache)
			if ok then
				self.cache = remain
				table.insert(result, resp)
				i = i + 1
			end
			if not ok or #remain == 0 then
				break
			end
		end
	end
	return result
end

function _M.subscribe(self, topic)
	assert(type(topic) == "string")
	self.topic[topic] = true
	self.pubsub = false
	tcp.setopts(self.conn, {active=false})
	
	local sub = ""
	if topic:match("([%?%*]+)") then
		sub = "psubscribe"
	else
		sub = "subscribe"
	end
	local meta  = getmetatable(_M)
	local func  = meta.__index({}, sub)
	local resp  = func(self, topic)
	self.pubsub = true
	tcp.setopts(self.conn, {active=self.active})

	return resp
end

function _M.unsubscribe(self, topic)
	assert(type(topic) == "string")
	assert(self.pubsub, "it's no in subscribe")
	self.pubsub = false
	tcp.setopts(self.conn, {active=false})

	local sub = ""
	if topic:match("([%?%*]+)") then
		sub = "punsubscribe"
	else
		sub = "unsubscribe"
	end
	local meta  = getmetatable(_M)
	local func  = meta.__index({}, sub)
	local resp  = func(self, topic)
	
	self.topic[topic] = nil

	if next(self.topic) then
		self.pubsub = true
	else
		self.pubsub = false
	end
	tcp.setopts(self.conn, {active=self.active})
	return resp
end


return redis
