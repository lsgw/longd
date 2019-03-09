local tcp = require "tcp"
local dns = require "dns"

local httpc = { 
	timeout = 60, 
	LIMIT   = 1024 * 1024 * 10,
}
function httpc.dns(host, port, ipv6)
	httpc.config = { 
		host = host,
		port = port,
		ipv6 = ipv6,
	}
	dns.server(host, port, ipv6)
end


function httpc.pack(host, method, url, version, header, body)
	local request_line   = method .. " " .. url .. " " .. version .. "\r\n"
	local request_header = ""

	if header then
		if not header.host then
			header.host = host
		end
		for k,v in pairs(header) do
			request_header = string.format("%s%s:%s\r\n", request_header, k, v)
		end
	else
		request_header = string.format("host:%s\r\n", host)
	end

	if body then
		request_header = string.format("%scontent-length:%d\r\n\r\n", request_header, #body)
		return request_line .. request_header .. body
	else
		request_header = string.format("%scontent-length:%d\r\n\r\n", request_header, 0)
		return request_line .. request_header
	end
end

function httpc.unpack(message)
	local response = { }

	local lb, le = message:find("^HTTP/%d%.%d%s+%d+%s+%w*\r\n", 1, false)
	if not lb then
		return nil, "no find http status"
	end
	local line = message:sub(lb, le)
	local code, info = line:match("^HTTP/%d%.%d%s+([%d]+)%s+(%a*)\r\n")
	response.code = code
	response.info = info
	response.header = { }
	
	
	local hb, he = message:find("\r\n\r\n", le+1, true)
	if not hb then
		return nil, "no find http header"
	end


	local header = message:sub(le+1, he)
	for str in header:gmatch("(.-)\r\n") do
		if str ~= "" then
			local k, v = str:match("^(.-)%s*:%s*(.-)$")
			response.header[k:lower()] = v
		end
	end

	local content_length = response.header["content-length"]
	if not content_length then
		return response
	else
		content_length = tonumber(content_length)
	end

	local body = message:sub(he+1, he+content_length)
	if #body ~= content_length then
		return nil, "no find http body"
	end

	response.body = body
	return response, message:sub(he+content_length+1)
end

--解码
function httpc.decodeURI(s)
    s = string.gsub(s, '%%(%x%x)', function(h) return string.char(tonumber(h, 16)) end)
    return s
end
--编码
function httpc.encodeURI(s)
    s = string.gsub(s, "([^%w%.%- ])", function(c) return string.format("%%%02X", string.byte(c)) end)
    return string.gsub(s, " ", "+")
end

function httpc.request(host, method, url, version, header, body)
	local dns_launch = true
	if not httpc.config and not dns.launch then
		dns.server()
		dns_launch = false
	end 
	local ip, port = host:match("^(%d+%.%d+%.%d+%.%d+):(%d+)$")
    if not ip or not port then
		ip = dns.resolve(host)
		port = 80
    else
    	port = tonumber(port)
    end
    if not httpc.config and not dns_launch then
    	dns.close()
    end
	
	assert(ip)
	local conn = tcp.connect({ip=ip, port=port, ipv6=false})
	assert(conn>0)
	
	local msg = httpc.pack(host, method, url, version, header, body)
	tcp.start(conn, {active=false, keepalive=true, nodelay=true})
	tcp.write(conn, msg)

	local message = ""
	local response
	local i = 1
    while true do
		local newmsg = tcp.read_timeout(conn, httpc.timeout)
		if not newmsg or #newmsg == 0 then
			tcp.close(conn)
			break
		end
		message = message .. newmsg
		if #message > httpc.LIMIT then
			tcp.close(conn)
			break
		end
		response = httpc.unpack(message)
		if response then
			tcp.close(conn)
			break
		end
	end
	return response
end

function httpc.get(host, url, header, body)
	return httpc.request(host, "GET", url, "HTTP/1.1", body)
end
function httpc.post(host, url, header, body)
	return httpc.request(host, "POST", url, "HTTP/1.1", body)
end



return httpc
