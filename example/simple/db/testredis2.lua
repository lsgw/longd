local system = require "system"
local redis  = require "redis"
local json   = require "rapidjson"

local db = redis.connect({ip="10.211.55.6"})
db:publish("foo", "oh my god")

db:close()