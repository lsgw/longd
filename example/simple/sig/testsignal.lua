local system = require "system"
local logger = require "logger"
local signal = require "signal"
local args   = {...}

logger.info("file testsignal.lua", args, system.self())
logger.info("signal action SIGUSR1")

local i = 0
signal.action("SIGUSR1", function(sig)
	logger.info(i, "happen sig ", sig, " ", signal.info())
	i = i + 1
	if i > 5 then
		signal.cancel(sig)
	end
end)

while true do
	system.sleep(3)
	logger.info("sleep 5")
end
