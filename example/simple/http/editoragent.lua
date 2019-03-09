local system = require "system"
local logger = require "logger"
local httpd  = require "httpd"
local json   = require "rapidjson"
local socket = require "socket"
local conn   = ...

local root = "../example/simple/http/editor"
local defaultsendbytes = 1024 * 1024

local function readtextfile(file, header)
	local msg = file:read("a")
	return 200, nil, msg
end


local function staticfile(path, header)
	local code, h, msg

	if path == "/" then
		path = "/index.html"
	end
	local file = io.open(root..path)
	if file then
		code, h, msg = readtextfile(file, header)
		file:close()
	else
		local html = [[
				404
			This is not the web page 
			you are looking for.
		]]
		code, h, msg = 404, nil, html
	end

	return code, h, msg
end

httpd.loop(conn, function(method, path, query, header, body)
	local code, h, msg
	if path=="/run.lua.script" and body then
		logger.info(path, "\n", body)
		h = { }
		h["Access-Control-Allow-Origin"] = "*"
		h["Access-Control-Allow-Headers"] = "Content-Type, Accept, Authorization"
		h["Access-Control-Allow-Methods"] = "GET,POST,PUT,DELETE,PATCH,OPTIONS"

		local luafile = tostring(system.self()).. "-" .. tostring(os.time()) .. ".lua"

		local f = io.open(luafile, "wb")
		f:write(body)
		f:close()

		local lua = socket.exe.open("../bin/lua", luafile)
		lua:start();
		local i = 0
		local resp = ""
		while i < 10 do
			logger.info("read ... start")
			local m = lua:read_timeout(4)
			if not m then
				logger.info("read ... timeout")
				break
			end
			if #m == 0 then
				logger.info("read ... ok")
				break
			end
			resp = resp .. m
			if #resp > 1024 then
				logger.info("read ... limit")
				break
			end
			i = i + 1
		end
		lua:close()
		os.remove(luafile)
		
		if #resp == 0 then
			code, h, msg = 200, h, "syntax error"
		else
			code, h, msg = 200, h, resp
		end
	else
		logger.info(path, body)
		code, h, msg = staticfile(path, header)
	end
	return code, h, msg
end)
