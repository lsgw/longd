local system = require "system"
local logger = require "logger"
local socket = require "socket"
local ip, port = ...

local ss = socket.tcp.listen({ip=ip, port=port, ipv6=false})
logger.info("httpd editoragent listen", ss:unwrap())
ss:start()

while true do
	local client = ss:accept();
	local agent = system.newservice("editoragent", client:unwrap())
	logger.info("new http request editoragent ", agent, client:unwrap())
end