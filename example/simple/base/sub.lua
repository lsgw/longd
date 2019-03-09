local system = require "system"
local logger = require "logger"
local args   = {...}

logger.info("file sub", args, system.self())
local a = 0
local b = 0
while true do
	system.receive {
		[{"json", "call"}] = function(type, pattern, ref, source, ping, i)
			assert(ping == "ping")
			a = os.time()
			logger.info("sub", ping, i)
			system.send(source, type, "resp", ref, system.self(), "pong " .. tostring(i))
		end,
		[{"after", 4}] = function(...)
			logger.info("timeout")
			b = os.time()
			logger.info(string.format("a=%d, b=%d, total=%d", a, b, b-a))
			system.exit()
		end,
	}
end
