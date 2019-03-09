local system  = require "system"
local logger  = require "logger"
local cluster = require "cluster"
local clusterd, proxy, node, destination = ...
assert(cluster.open())

logger.info("new clusterd_alias handle", system.self())
while true do
	system.receive {
		[{nil, "call"}] = function(type, pattern, ref, source, ...)
			local rets = {pcall(system.call, proxy, "lua", "call", destination, type, ...)}
			local ok = table.remove(rets, 1)
			if not ok then
				rets = {nil, 8, "not find proxy " .. tostring(proxy) .. "(" .. node .. ")"}
			end
			if not rets[1] and rets[2] < 4 then
				system.cast(clusterd, "lua", "disconnect", node)
			end
			if not rets[1] then
				logger.info("clusterd_alias", proxy, node, destination, table.unpack(rets))
			end
			system.send(source, type, "resp", ref, system.self(), table.unpack(rets))
		end,
		[{nil, "cast"}] = function(type, pattern, ref, source, ...)
			local rets = {pcall(system.cast, proxy, "lua", "cast", destination, type, ...)}
			local ok = table.remove(rets, 1)
			if not ok then
				rets = {nil, 9, "not find proxy " .. tostring(proxy) .. "(" .. node .. ")"}
			end
			if not rets[1] then
				logger.info("clusterd_alias", proxy, node, destination, table.unpack(rets))
			end
		end,
	}
end