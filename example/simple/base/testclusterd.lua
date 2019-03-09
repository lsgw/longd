local system  = require "system"
local logger  = require "logger"
local cluster = require "cluster"
local handle = cluster.open(...)

logger.info("launch cluster", handle)

local i = 1
while true do
	local node = cluster.node("testdb")
	if not node then
		logger.info("wait launch testdb cluster")
		system.sleep(1)
	else
		local simpledb, code, msg = cluster.query(node, "simpledb")
		if simpledb then
			logger.info("simpledb handle", simpledb)
			local value1, code1, msg1 = cluster.call(node, simpledb, "lua", "get", "a")
			logger.info("get a", value1, code1, msg1)
			local value2, code2, msg2 = cluster.call(node, simpledb, "lua", "set", "a", i)
			logger.info("set a", value2, code2, msg2)
			i = i + 1
		else
			logger.info("can't find simpledb service", code, msg)
			system.sleep(1)
		end
	end
end
