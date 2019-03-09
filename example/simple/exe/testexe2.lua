local system = require "system"
local logger = require "logger"
local exe    = require "exe"
local args   = {...}

logger.info("file testexe.lua", args, system.self())

local id = exe.open("../bin/sum")
logger.info("exe open id ", id)
exe.start(id)

local msg = exe.read(id)
logger.info("msg", msg)
system.sleep(10);
logger.info("close exe")

exe.close(id)
