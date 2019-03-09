local system  = require "system"
local logger  = require "logger"
local cluster = require "cluster"

local handle = cluster.open(...)

logger.info("launch testclusterd_alias", handle)

local i = 1
while true do
	local node = cluster.node("testdb")
	if not node then
		logger.info("wait launch testdb cluster")
		system.sleep(1)
	else
		logger.info("testdb cluster ok")
		local handle, code, msg = cluster.query(node, "simpledb")
		if handle then
			local simpledb = cluster.alias(node, handle)
			while true do
				local value1, code1, msg1 = system.call(simpledb, "lua", "get", "a")
				logger.info("get a", value1, code1, msg1)
				local value2, code2, msg2 = system.call(simpledb, "lua", "set", "a", i)
				logger.info("set a", value2, code2, msg2)
				i = i + 1
				if not value1 or not value2 then
					system.kill(simpledb)
					break
				end
			end
		else
			logger.info("can't find simpledb service", code, msg)
			system.sleep(1)
		end
	end
end
