local system = require "system"
local tcp    = require "tcp"

local http_status_msg = {
	[100] = "Continue",
	[101] = "Switching Protocols",
	[200] = "OK",
	[201] = "Created",
	[202] = "Accepted",
	[203] = "Non-Authoritative Information",
	[204] = "No Content",
	[205] = "Reset Content",
	[206] = "Partial Content",
	[300] = "Multiple Choices",
	[301] = "Moved Permanently",
	[302] = "Found",
	[303] = "See Other",
	[304] = "Not Modified",
	[305] = "Use Proxy",
	[307] = "Temporary Redirect",
	[400] = "Bad Request",
	[401] = "Unauthorized",
	[402] = "Payment Required",
	[403] = "Forbidden",
	[404] = "Not Found",
	[405] = "Method Not Allowed",
	[406] = "Not Acceptable",
	[407] = "Proxy Authentication Required",
	[408] = "Request Time-out",
	[409] = "Conflict",
	[410] = "Gone",
	[411] = "Length Required",
	[412] = "Precondition Failed",
	[413] = "Request Entity Too Large",
	[414] = "Request-URI Too Large",
	[415] = "Unsupported Media Type",
	[416] = "Requested range not satisfiable",
	[417] = "Expectation Failed",
	[500] = "Internal Server Error",
	[501] = "Not Implemented",
	[502] = "Bad Gateway",
	[503] = "Service Unavailable",
	[504] = "Gateway Time-out",
	[505] = "HTTP Version not supported",
}


local httpd = { 
	cache = "",
	LIMIT = 1024 * 1024 * 10,
}

function httpd.unpack(message)
	local request = { }

	local lb, le = message:find("^%u+%s+.-%s+HTTP/[%d%.]+\r\n", 1, false)
	if not lb then
		return nil, "no find http status"
	end
	local line = message:sub(lb, le)
	local method, url, version = line:match("^(%u+)%s+(.-)%s+(HTTP/[%d%.]+)\r\n")
	local path, query = url:match("([^?]*)%??(.*)")
	request.method  = method
	request.version = version
	request.path    = path
	request.header  = { }
	if #query > 0 then
		request.query = { }
		for k,v in query:gmatch("(.-)=([^&]*)&?") do
			request.query[k] = v
		end
	end
	
	local hb, he = message:find("\r\n\r\n", le+1, true)
	if not hb then
		return nil, "no find http header"
	end

	local header = message:sub(le+1, he)
	for str in header:gmatch("(.-)\r\n") do
		if str ~= "" then
			local k, v = str:match("^(.-)%s*:%s*(.-)$")
			request.header[k:lower()] = v
		end
	end
	local content_length = request.header["content-length"]
	if not content_length then
		return request, message:sub(he+1)
	else
		content_length = tonumber(content_length)
	end

	local body = message:sub(he+1, he+content_length)
	if #body ~= content_length then
		return nil, "no find http body"
	end

	request.body = body
	return request, message:sub(he+content_length+1)
end

function httpd.pack(version, code, header, body)
	assert(http_status_msg[code], "no match code " .. tostring(code))
	local request_line   = version .. " " .. tostring(code) .. " " .. http_status_msg[code] .. "\r\n"
	local request_header = ""

	if header then
		for k,v in pairs(header) do
			request_header = string.format("%s%s:%s\r\n", request_header, k, v)
		end
	end

	if body then
		request_header = string.format("%scontent-length:%d\r\n\r\n", request_header, #body)
		return request_line .. request_header .. body
	else
		request_header = string.format("%scontent-length:%d\r\n\r\n", request_header, 0)
		return request_line .. request_header
	end
end

function httpd.loop(conn, func)
	tcp.start(conn, {active=true})
	local i = 0
	while true do
	 	system.receive {
			[{"tcp", tcp.type.kRecv, conn, 0}] = function(type, subtype, id, session, msg)
				if #msg == 0 then
					tcp.close(id)
					httpd.cache = ""
					system.exit()
				end
				httpd.cache = httpd.cache .. msg
				if string.len(httpd.cache) > httpd.LIMIT then
					tcp.close(id)
					httpd.cache = ""
					system.exit()
				end
				local request, remain = httpd.unpack(httpd.cache)
				if not request then
					return nil
				end
				httpd.cache = remain
				local code, header, body = func(request.method, request.path, request.query, request.header, request.body)
				if code then
					local response = httpd.pack(request.version, code, header, body)
					tcp.write(id, response)
				end
				if not request.header["connection"] or string.lower(request.header["connection"]) ~= "keep-alive" then
					tcp.shutdown(id)
					httpd.cache = ""
					system.exit()
				end
				i = 0
			end,
			[{"after", 60}] = function(...)
				if i > 3 then
					system.exit()
				end
				i = i + 1
			end,
		}
	end
end

return httpd