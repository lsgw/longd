local system = require "system"
local logger = require "logger"
local httpd  = require "httpd"
local conn   = ...

local root = "../example/simple/http/media"
local defaultsendbytes = 1024 * 1024


local function range(str)
	if str then
		str = string.lower(str)
		logger.info("range", str)
		local s, e = string.match(str, "bytes=(%d*)-(%d*)")
		return true, tonumber(s), tonumber(e)
	else
		return nil
	end
end


local function readmediafile(file, header)
	local h = {}
	h["Content-Type"] = "video/mp4"
	h["Accept-Ranges"] = "bytes"

	local total = file:seek("end")

	local yes, s, e = range(header.range)
	if not yes then
		s = 0
		e = defaultsendbytes
		h["Content-Range"] = string.format("bytes %d-%d/%d", s, e, total)
		file:seek("set", s)
		local msg  = file:read(e - s + 1)
		-- local msg = file:read("a")
		logger.info(string.format("bytes %d-%d/%d", s, e, total))
		return 206, h, msg
	else
		if not e then
			e = s + defaultsendbytes
		end
		if e >= total then
			e = total - 1
		end
		if s > e then
			s = e
		end

		h["Content-Range"] = string.format("bytes %d-%d/%d", s, e, total)
		file:seek("set", s)

		if (e - s + 1) > defaultsendbytes then
			e = s + defaultsendbytes
		end
		local msg  = file:read(e - s + 1)
		logger.info(string.format("bytes %d-%d/%d", s, e, total))

		return 206, h, msg
	end
end

local function readtextfile(file, header)
	local msg = file:read("a")
	return 200, nil, msg
end


local function staticfile(path, header)
	local code, h, msg
	local suffix = path:match(".+%.(%w+)$")
	local file = io.open(root..path)
	if file and suffix == "mp4" then
		code, h, msg = readmediafile(file, header)
		file:close()
	elseif file and path ~= "/" then
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
	local html = [[
		this is my freind
		yes
	]]
	local code, h, msg
	if query then
		logger.info(path, body)
		code, h, msg = 200, nil, html
	else
		logger.info(path, body)
		code, h, msg = staticfile(path, header)	
	end
	return code, h, msg
end)