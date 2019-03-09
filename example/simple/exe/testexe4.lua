local system = require "system"
local logger = require "logger"
local exe    = require "exe"
local args   = {...}

logger.info("file testexe.lua", args, system.self())

local id = exe.open("../bin/sum")
logger.info("exe open id ", id)
exe.start(id)

while true do
	local msg = exe.read_timeout(id, 2)
	if not msg then
		logger.info("read timeout")
		break
	end
	if #msg == 0 then
		logger.info("close")
		exe.close(id)
		break
	end
	logger.info("msg", msg)
end