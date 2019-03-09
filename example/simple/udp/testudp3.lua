local system = require "system"
local logger = require "logger"
local udp    = require "udp"
local args   = {...}

logger.info("file udp", args, system.self())

local id = udp.open({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("udp open id = ", id)
udp.start(id, {active=true})

local i = 0
while true do
	system.receive {
		[{"udp", udp.type.kRecv, id}] = function(type, subtype, id, session, msg, addr)
			logger.info("(msg, addr)", msg, addr)
			udp.sendto(id, addr, msg)
			msg = string.match(msg, "(%a+)")
			if msg == "exit" then
				udp.close(id)
				system.exit()
			end
			i = 0
		end,
		[{"after", 5}] = function()
			logger.info("udp recv timeout", i)
			i = i + 1
			if i > 10 then
				system.exit()
			end
		end,
	}
end