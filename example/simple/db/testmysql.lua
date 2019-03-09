local system = require "system"
local mysql  = require "mysql"
local json   = require "rapidjson"

local function serialize(obj)
    local lua = ""
    local t = type(obj)
    if t == "number" then
        lua = lua .. obj
    elseif t == "boolean" then
        lua = lua .. tostring(obj)
    elseif t == "string" then
        lua = lua .. string.format("%q", obj)
    elseif t == "table" then
        lua = lua .. "{"
        for k, v in pairs(obj) do
            lua = lua .. "[" .. serialize(k) .. "]=" .. serialize(v) .. ","
        end
        local metatable = getmetatable(obj)
            if metatable ~= nil and type(metatable.__index) == "table" then
            for k, v in pairs(metatable.__index) do
                lua = lua .. "[" .. serialize(k) .. "]=" .. serialize(v) .. ","
            end
        end
        lua = lua .. "}"
    elseif t == "nil" then
        return nil
    else
        error("can not serialize a " .. t .. " type.")
    end
    return lua
end


local db = mysql.connect({
	host="127.0.0.1",
	port=3306,
	database="test",
	user="root",
	password="123"
})


local rr1 = db:query("update vendors set vend_name='friend' where vend_id=1")
print("rr1", json.encode(rr1))

local rr2 = db:query("update vendors sevend_name='friend' where vend_id=1")
print("rr2", json.encode(rr2))


local ret1 = db:query("select * from vendors")
print("ret1", json.encode(ret1))

local ret2 = db:query("select 2001%7;")
print("ret2", json.encode(ret2))

db:close()
