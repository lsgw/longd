local system = require "system"
local logger = require "logger"
local socket = require "socket"
local args   = {...}

logger.info("file testtcp3.lua", args, system.self())

local st = socket.tcp.listen({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("tcp listen id", st:info())
st:start()

while true do
	local client = st:accept();
	logger.info("tcp client", client:info())
	client:start()

	local msg = client:read()
	logger.info("tcp client msg", msg)
	client:write(msg)
	client:shutdown()
end