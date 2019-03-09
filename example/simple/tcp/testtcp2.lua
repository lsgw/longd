local system = require "system"
local logger = require "logger"
local tcp    = require "tcp"
local args   = {...}

logger.info("file testtcp2.lua", args, system.self())

local listenid = tcp.listen({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("tcp listenid", tcp.info(listenid))
tcp.start(listenid, {active=true})

local i = 0
while true do
	system.receive {
		[{"tcp", tcp.type.kAccept, listenid, 0}] = function(type, subtype, listenid, session, clientid, addr)
			tcp.start(clientid, {active=true})
			tcp.low_water_mark(clientid, true, 0)
			logger.info("new client id", clientid, addr)
		end,
		[{"tcp", tcp.type.kRead}] = function(type, subtype, id, session, msg)
			if #msg == 0 then
				tcp.close(id)
				logger.info("peer client close", id)
			elseif msg:match("(%a+)") == "exit" then
				tcp.write(id, msg)
				tcp.shutdown(id)
				logger.info("need client close", id)
			else
				tcp.write(id, msg)
				logger.info("read client", id, msg)
			end
		end,
		[{"tcp", tcp.type.kLowWaterMark}] = function(type, subtype, id, session, value)
			logger.info("low water", id, value)
		end,
		[{"tcp", tcp.type.kHighWaterMark}] = function(type, subtype, id, session, value)
			logger.info("high water", id, value)
		end,
		[{"after", 10}] = function()
			logger.info("tcp recv timeout", i)
			i = i + 1
			-- if i > 10 then
			-- 	system.exit()
			-- end
		end,
	}
end