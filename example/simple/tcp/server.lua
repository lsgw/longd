local system = require "system"
local logger = require "logger"
local socket = require "socket"
local args   = {...}

logger.info("file server.lua", args, system.self())

local ss = socket.tcp.listen({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("tcp listen id", ss:info())
ss:start()

while true do
	local client = ss:accept();
	logger.info("(client, addr)", client:unwrap())
	system.newservice("agent", client:unwrap())
end
