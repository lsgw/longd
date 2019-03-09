local system = require "system"
local logger = require "logger"
local udp    = require "udp"
local args   = {...}

logger.info("file udp", args, system.self())

local id = udp.open({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("udp open id = ", id)
udp.start(id)


while true do
	local msg, addr = udp.recvfrom_timeout(id, 10)
	if msg then
		logger.info("(msg, addr)", msg, addr)
		udp.sendto(id, addr, msg)
		msg = string.match(msg, "(%a+)")
		if msg == "exit" then
			udp.close(id)
			break
		end
	else
		logger.info("recv timeout")
	end
end