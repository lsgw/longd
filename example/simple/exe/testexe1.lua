local system = require "system"
local logger = require "logger"
local exe    = require "exe"
local args   = {...}

logger.info("file testexe.lua", args, system.self())

local id = exe.open("../bin/sum")
logger.info("exe open id", id)
exe.start(id)

while true do
	local msg = exe.read(id)
	logger.info("msg", msg)
	if #msg == 0 then
		exe.close(id)
		break
	end
end