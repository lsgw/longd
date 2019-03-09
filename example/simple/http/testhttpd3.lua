local system = require "system"
local logger = require "logger"
local socket = require "socket"
local ip, port = ...

local ss = socket.tcp.listen({ip=ip, port=port, ipv6=false})
logger.info("httpd benchmark listen", ss:unwrap())
ss:start()

local agent = {}
for i= 1, 32 do
	agent[i] = system.newservice("bagent")
end

local balance = 1
while true do
	local client = ss:accept();
	--logger.info(string.format("%s connected, pass it to agent : %010d", client.addr.ip, agent[balance]))
	system.cast(agent[balance], "lua", "connup", client.id)
	balance = balance + 1
	if balance > #agent then
		balance = 1
	end
end