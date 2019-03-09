local system = require "system"
local logger = require "logger"
local dns    = require "dns"
local args   = {...}


local resolve_list = {
	"https://www.baidu.com",
	"github.com",
	"stackoverflow.com",
	"lua.org",
	"lua.com",
	"www.reddit.com",
	"www.freecodecamp.org",
	"www.what.org",
}

dns.server()
dns.timeout = 0.1

for _, host in ipairs(resolve_list) do
	local ip = dns.resolve(host)
	logger.info(host, "-->", ip)
	-- timer.sleep(0.1)
end

dns.close()