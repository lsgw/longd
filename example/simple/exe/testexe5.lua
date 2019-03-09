local system = require "system"
local logger = require "logger"
local exe    = require "exe"
local args   = {...}

logger.info("file testexe.lua", args, system.self())

local id = exe.open("../bin/sum", 1, "friend", 4.23)
exe.start(id, {active=true})

local i = 1
while true do
	system.receive {
		[{"exe", exe.type.kRead, id, 0}] = function(type, subtype, id, session, ...)
			logger.info("msg ", ...)
		end,
		[{"after", 2}] = function()
			if i > 10 then
				system.exit()
			end
			exe.write(id, tostring(i) .. " " .. tostring(i+1))
			i = i + 1
		end,
	}
end
