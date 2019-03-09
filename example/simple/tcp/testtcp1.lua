local system = require "system"
local logger = require "logger"
local tcp    = require "tcp"
local args   = {...}

logger.info("file testtcp1.lua", args, system.self())

local id = tcp.listen({ip="127.0.0.1", port=8086, ipv6=false})
logger.info("tcp listen id", tcp.info(id))
tcp.start(id)

while true do
	local client, addr = tcp.accept(id);
	logger.info("tcp client", client, addr)
	tcp.start(client)

	local msg = tcp.read(client)
	logger.info("tcp client msg", msg)
	tcp.write(client, msg)
	tcp.shutdown(client)
end