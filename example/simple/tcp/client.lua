local system = require "system"
local logger = require "logger"
local socket = require "socket"
local args   = {...}

logger.info("file client.lua", args, system.self())

local cc = socket.tcp.connect({ip=args[1], port=args[2], ipv6=false})
logger.info("tcp connect id", cc:unwrap())
cc:start()

while true do
	local msg = cc:read()
	logger.info("tcp client msg", msg)
	cc:write(msg)

	if #msg == 0 or msg:match("(%a+)") == "exit" then
		cc:shutdown()
		break
	end
end
