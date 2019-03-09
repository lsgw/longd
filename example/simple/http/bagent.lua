local system = require "system"
local logger = require "logger"
local httpd  = require "httpd"
local tcp    = require "tcp"
local map    = { }
local CMD    = { }

--print("ok start client", system.self(), system.now().millis())

function CMD.request(method, path, query, header, body)
	local msg = [[
		this is my freind
		yes
	]]
	--logger.info(path, body)
	return 200, nil, msg
end
function CMD.connup(conn)
	tcp.start(conn, {active=true})
	assert(not map[conn])
	map[conn] = { cache = "", count = 0, }
end
function CMD.conndown(conn)
	--print("tcp down", conn)
	if map[conn] then
		tcp.close(conn)
		map[conn] = nil
	end
end



local i = 0
while true do
 	system.receive {
		[{"tcp", tcp.type.kRecv, nil, 0}] = function(type, subtype, id, session, msg)
			--print("tcp msg", id, #msg)
			if #msg == 0 then
				CMD.conndown(id)
				return
			end
			if not map[id] then
				return
			end
			map[id].cache = map[id].cache .. msg
			if string.len(map[id].cache) > httpd.LIMIT then
				CMD.conndown(id)
				return
			end
			local request, remain = httpd.unpack(map[id].cache)
			if not request then
				return nil
			end
			map[id].cache = remain
			local code, header, body = CMD.request(request.method, request.path, request.query, request.header, request.body)
			if code then
				local response = httpd.pack(request.version, code, header, body)
				tcp.write(id, response)
			end
			if not request.header["connection"] or string.lower(request.header["connection"]) ~= "keep-alive" then
				CMD.conndown(id)
				return
			end
			i = 0
		end,
		[{"lua", "cast"}] = function(type, pattern, ref, source, f, ...)
			--logger.info("agent", f,  ...)
			local func = CMD[f]
			if func then
				func(...)
			end
		end,
		-- [{"after", 60}] = function(...)
		-- 	if i > 3 then
		-- 		CMD.connup(conn)
		-- 	end
		-- 	i = i + 1
		-- end,
	}
end