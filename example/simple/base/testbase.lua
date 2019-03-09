local system = require "system"
local logger = require "logger"

logger.info("file testbase", ...)


local handle = system.query("testbase");
logger.info("testbase", handle)

local i = 0
while true do
	-- system.receive {
	-- 	[{"lua"}] = function(...)
	-- 		logger.info(...)
	-- 	end,
	-- 	[{"after", 2}] = function(...)
	-- 		logger.info("timeout", i, ...)
	-- 		i = i + 1
	-- 	end,
	-- }
	system.sleep(3)
	logger.info("timeout", i)
	i = i + 1
end