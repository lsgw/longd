local system = require "system"
local logger = require "logger"
local args   = {...}

local sub = system.uniqueservice("sub", 123, {a="hello", b=123.5})
logger.info("new service = ", sub)

local i = 1
while true do
	local sss = system.call(sub, "json", "ping", i)
	logger.info(i, sss)
	i = i + 1
	if i > 10 then
		break
	end
	system.sleep(1)
end