local system  = require "system"
local logger  = require "logger"

local s = system.query("simpledb")
if s then
	system.exit()
else
	system.register(system.self(), "simpledb")
end

local db = { }
local CMD = { }
function CMD.set(key, value)
	local last = db[key]
	db[key] = value
	return last
end
function CMD.get(key)
	return db[key]
end
function CMD.ping()
	return "pong"
end

while true do
	system.receive {
		[{"lua"}] = function(type, pattern, ref, source, f, ...)
			assert(pattern == "call")
			logger.info("simpledb command", f, ...)
			local func = CMD[string.lower(f)]
			local rets = {nil, "no value"}
			if func then
				rets = {func(...)}
			end
			system.send(source, type, "resp", ref, system.self(), table.unpack(rets))
		end
	}
end