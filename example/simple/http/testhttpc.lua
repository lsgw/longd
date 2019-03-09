local system = require "system"
local logger = require "logger"
local httpc  = require "httpc"

local resp = httpc.get("www.baidu.com", "/")
if resp then
	logger.info("httpc get ", resp.code, resp.info, resp.header)
else
	logger.info("httpc get ", "timeout")
end

system.sleep(10)
