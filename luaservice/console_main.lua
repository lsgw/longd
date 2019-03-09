local system = require "system"
local logger = require "logger"
local socket = require "socket"

local ip, port = ...

local console = socket.tcp.listen({ip=ip, port=port, ipv6=false})
console:start()
logger.info("console listen ", console)

while true do
	local client = console:accept();
	local agent = system.newservice("console_agent", client:unwrap())
end
