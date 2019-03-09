local system = require "system"
local logger = require "logger"
local httpd  = require "httpd"
local conn   = ...

--print("ok start client", system.self(), system.now().millis())
httpd.loop(conn, function(method, path, query, header, body)
	local msg = [[
		this is my freind
		yes
	]]
	--logger.info(path, body)
	return 200, nil, msg
end)