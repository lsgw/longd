local system = require "system"
local redis  = require "redis"
local json   = require "rapidjson"

local db = redis.connect({ip="10.211.55.6"})

print("ping",  db:ping())
print("srandmember", table.unpack(db:srandmember("letters", 2)))
print("set b", db:set("b", "world"))
print("get b", db:get("b"))
print("del b", db:del("b"))
print("get b", db:get("b"))
print("get a", db:get("a"))
print("get c", db:get("c"))
print("xx",    db:xx())
print("get a", db:get("a"))
-- print("sadd letters a b", db:sadd("letters", "a", "b"))
print("multi", db:multi())
print("set k", db:set("k", 45))
print("set u", db:set("u", "friend"))
print("exec", table.unpack(db:exec()))
print("get u", db:get("u"))


print("set key 1", db:set("key", 1))
print("watch key", db:watch("key"))
print("set key 2", db:set("key", 2))
print("multi",     db:multi())
print("set key 3", db:set("key", 3))
print("exec",      db:exec())
print("get key",   db:get("key"))

local rets = db:pipeline({"set", "friend", "tonny"}, {"get", "a"}, {"get", "k"})
print("pipeline", table.unpack(rets))


print("subscribe", table.unpack(db:subscribe("f*")))
print("subscribe", table.unpack(db:subscribe("bar")))

local loop  = true
local function on_topic(type, ...)
	print("on_topic", type, ...)
end

db:opts({active=true})
local i = 0
while loop do
	local packet = system.receive({type="tcp", source=db:id()}, 1)
	match(packet) {
		["tcp"] = function(id, session, subtype, msg)
			assert(id == db:id())
			if #msg == 0 then
				loop = false
			else
				on_topic(db:unpack(msg))
			end
			i = 0
		end,
		["time"] = function(...)
			print("time out", i)
			if i == 2 then
				print("unsubscribe", table.unpack(db:unsubscribe("f*")))
			end
			if i == 4 then
				print("unsubscribe", table.unpack(db:unsubscribe("bar")))
				loop = false
			end
			i = i + 1
		end,
	}
end
db:opts({active=false})
db:close()
