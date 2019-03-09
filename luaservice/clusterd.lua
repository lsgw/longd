local system = require "system"
local logger = require "logger"
local tcp    = require "tcp"

local localaddr, localname, localhide, remoteaddr = ...
local nodes  = { }
local CMD    = { }
local heartbeatpeernodes = { }

local function connect(node, agent)
	if nodes[node] then
		assert(not nodes[node].agent)
		assert(nodes[agent])
		nodes[agent] = nil
		nodes[node].count = 0
		nodes[node].agent = agent
		return true, nodes[node]
	end
	local ip, port = node:match("^(%d+%.%d+%.%d+%.%d+):(%d+)$")
	if not ip or not port then
		return false, "connect node addr error : " .. node
	end
	port = tonumber(port)

	local id = tcp.connect({ip=ip, port=port, ipv6=false})
	if not id then
		return false, "can't connect node addr : " .. node
	end
	local handle = system.newservice("clusterd_proxy", id, system.self(), localaddr, localname, localhide, node)
	assert(handle)
	nodes[node] = { }
	nodes[node].proxy = handle
	nodes[node].count = 0
	if agent then
		assert(nodes[agent])
		nodes[agent] = nil
		nodes[node].agent = agent
	end
	return true, nodes[node]
end

local function checkheartbeat()
	local validnodes = { }
	for node, value in pairs(nodes) do
		if type(node) == "string" and value.proxy and value.agent then
			if nodes[node].count > 5 then
				system.kill(value.proxy)
				system.kill(value.agent)
				nodes[node] = nil
			else
				validnodes[node] = value
				nodes[node].count = nodes[node].count + 1
			end
		end
		if type(node) == "string" and value.proxy and not value.agent then
			if nodes[node].count > 5 then
				system.kill(value.proxy)
				nodes[node] = nil
			else
				nodes[node].count = nodes[node].count + 1
			end
		end
		if type(node) == "number" then
			if nodes[node].count > 5 then
				system.kill(node)
				nodes[node] = nil
			else
				nodes[node].count = nodes[node].count + 1
			end
		end
	end

	local heartbeatmsg = { }
	if not localhide then
		for node, value in pairs(validnodes) do
			if not value.hide then
				heartbeatmsg[node] = value
			end
		end
	end
	for node, value in pairs(validnodes) do
		system.cast(value.proxy, "lua", "heartbeat", heartbeatmsg)
	end
end

local function checknewnode()
	local pendingnodes = { }
	if remoteaddr and not nodes[remoteaddr] then
		pendingnodes[remoteaddr] = { }
	end
	if not localhide then
		for peeraddr, peernodes in pairs(heartbeatpeernodes) do
			for node, value in pairs(peernodes) do
				if not nodes[node] and node ~= localaddr then
					pendingnodes[node] = { }
				end
			end
		end
	end
	for node, value in pairs(pendingnodes) do
		local ok, info = connect(node)
	end
	heartbeatpeernodes = { }
end


local function request(node, ...)
	local rets
	if nodes[node] and nodes[node].agent then
		rets = {system.call(nodes[node].proxy, "lua", ...)}
	else 
		rets = {nil, 5, "can't find node " .. node}
	end
	if not rets[1] and rets[2] < 4 and nodes[node] and nodes[node].agent then
		system.kill(nodes[node].proxy)
		system.kill(nodes[node].agent)
		nodes[node] = nil
	end
	if not rets[1] then
		logger.info(table.unpack(rets))
	end
	return table.unpack(rets)
end


function CMD.query(node, name)
	return request(node, "query", name)
end
function CMD.call(node, handle, type, ...)
	return request(node, "call", handle, type, ...)
end
function CMD.cast(node, handle, type, ...)
	if nodes[node] and nodes[node].agent then
		system.cast(nodes[node].proxy, "lua", "cast", handle, type, ...)
	end
end
function CMD.timeout(node, s)
	if nodes[node] and nodes[node].agent then
		system.cast(nodes[node].proxy, "lua", "timeout", s)
	end
end
function CMD.alias(node, handle)
	if nodes[node] and nodes[node].agent then
		return system.newservice("clusterd_alias", system.self(), nodes[node].proxy, node, handle)
	else
		return nil, "can't find node" .. node
	end
end
function CMD.nodes()
	local validnodes = { }
	for node, value in pairs(nodes) do
		if type(node) == "string" and value.proxy and value.agent then
			validnodes[node] = value
		end
	end
	return validnodes
end
function CMD.node(name)
	for node, value in pairs(nodes) do
		if type(node) == "string" and value.proxy and value.agent and value.name == name then
			return node
		end
	end
	return nil, "can't find node " .. name
end
function CMD.disconnect(peeraddr)
	if nodes[peeraddr] and nodes[peeraddr].agent then
		system.kill(nodes[peeraddr].proxy)
		system.kill(nodes[peeraddr].agent)
		nodes[peeraddr] = nil
	end
end
function CMD.remote(handle, peeraddr, peername, peerhide)
	if nodes[peeraddr] and nodes[peeraddr].agent then
		system.kill(nodes[peeraddr].proxy)
		system.kill(nodes[peeraddr].agent)
		nodes[peeraddr] = nil
	end
	local ok, info = connect(peeraddr, handle)
	if ok then
		nodes[peeraddr].name = peername
		nodes[peeraddr].hide = peerhide
	else
		system.kill(handle)
		nodes[handle] = nil
	end
end
function CMD.heartbeat(handle, peeraddr, peernodes)
	if nodes[peeraddr] and nodes[peeraddr].agent then
		nodes[peeraddr].count = 0
		heartbeatpeernodes[peeraddr] = peernodes
	end
end




local heartbeat = 1 -- second
local lasttime = system.now().second()
local ip, port = localaddr:match("^(%d+%.%d+%.%d+%.%d+):(%d+)$")
local listen = tcp.listen({ip=ip, port=port, ipv6=false})
tcp.start(listen, {active=true})
while true do
	local timeout = false
	system.receive {
		[{"lua", "call"}] = function(type, pattern, ref, source, f, ...)
			logger.info("clusterd command", type, pattern, ref, source, f, ...)
			local func = CMD[f]
			local rets = {nil, 6, "no find func " .. f}
			if func then
				rets = {func(...)}
			end
			system.send(source, type, "resp", ref, system.self(), table.unpack(rets))
		end,
		[{"lua", "cast"}] = function(type, pattern, ref, source, f, ...)
			logger.info("clusterd command", type, pattern, ref, source, f, ...)
			local func = CMD[f]
			if func then
				func(...)
			end
		end,
		[{"tcp", tcp.type.kAccept}] = function(type, subtype, listen, session, client, addr)
			logger.info("clusterd accept", type, subtype, listen, session, client, addr)
			local handle = system.newservice("clusterd_agent", client, system.self(), addr)
			nodes[handle] = { }
			nodes[handle].count = 0
		end,
		[{"after", heartbeat}] = function()
			timeout = true
		end,
	}
	local currtime = system.now().second()
	if timeout or currtime - lasttime >= heartbeat then
		logger.info("checkheartbeat", currtime, lasttime)
		checkheartbeat()
		checknewnode()
		lasttime = currtime
	end
end
