local system = require "system"
local logger = require "logger"
local socket = require "socket"
local args   = {...}

logger.info("file testexe.lua", args, system.self())

local so = socket.exe.open("../bin/sum")
logger.info("exe open ", so:unwrap())
so:start()

so:write("friend")

local msg = so:read()
logger.info("msg", msg)
system.sleep(10);
logger.info("close exe")

so:close()