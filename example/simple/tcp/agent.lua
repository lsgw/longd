local system = require "system"
local logger = require "logger"
local socket = require "socket"
local agent  = socket.tcp.wrap(...)

logger.info("file agent.lua", agent:unwrap())
agent:start()

while true do
	local msg = agent:read()
	logger.info("tcp agent msg", msg)
	agent:write(msg)

	if #msg == 0 or msg:match("(%a+)") == "exit" then
		agent:shutdown()
		break
	end
end