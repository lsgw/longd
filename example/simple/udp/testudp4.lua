local system = require "system"
local logger = require "logger"
local socket = require "socket"
local args   = {...}

local su = socket.udp.open({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("udp open id", su:info())
su:start()

while true do
	local msg, addr = su:recv()
	logger.info("(msg, addr)", msg, addr)
	su:send(addr, msg)
	msg = string.match(msg, "(%a+)")
	if msg == "exit" then
		su:close()
		break
	end
end