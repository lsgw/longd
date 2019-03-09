local system = require "system"
local logger = require "logger"
local socket = require "socket"
local json   = require "rapidjson"
local signal = require "signal"
local trace  = require "trace"
local agent  = socket.tcp.wrap(...)

do
	system.protocol.register({
		name     = "debug",
		id       = system.protocol.MSG_TYPE_DEBUG,
		pack     = function(...) return json.encode_to_lightuserdata({...}) end,
	})
end
local debug = setmetatable({ _VERSION = '5.34' }, { __index = function(t, k)
	local cmd = string.lower(k)
	local f = function(handle, ...)
		local ref = system.ref()
		local succ, err = system.send(handle, system.protocol.MSG_TYPE_DEBUG, "calldebug", ref, system.self(), cmd, ...)
		assert(succ, tostring(err))
		return system.receive {
			[{"trace", "respdebug", ref, handle}] = function(type, resp, ref, source, ...)
				return ...
			end
		}
	end
	return f
end})



local function format_addr(addr)
	if not addr then
		return nil
	end
	local handle = tonumber(addr)
	if not handle then
		return nil
	end
	if  handle < 3 then
		return nil
	end
	return handle
end
local function format_args(...)
	local args = { }
	for i, v in ipairs({...}) do
		if v == "true" then
			table.insert(args, true)
		elseif v == "false" then
			table.insert(args, false)
		elseif tonumber(v) then
			table.insert(args, tonumber(v))
		else
			table.insert(args, v)
		end
	end
	return args
end
local function format_table(info)
	local str = ""
	str = str .. "--------------------------------------------------\n"
	for k, v in pairs(info) do
		if type(v) == "table" then
			str = str .. string.format("%10s: %s\n", k, json.encode(v))
		elseif k == "owner" then
			str = str .. string.format("%10s: %08x\n", k, v)
		else
			str = str .. string.format("%10s: %s\n", k, tostring(v))
		end
	end
	str = str .. "--------------------------------------------------"
	return str
end

local CMD = { }
function CMD.help( ... )
	local h = {
		exit     = "exit console",
		list     = "list all the service",
		kill     = "kill service, exp: kill 4",
		info     = "get service infomation, exp: info 7",
		gc       = "lua service garbage collect, exp: gc 9",
		shutdown = "system shutdown",
		profile  = "set profile, exp: profile 1 true",
		ps       = "all service status",
		ping     = "ping addr RTT， exp: ping 6",
		call     = "call service， exp: call addr proto ...",
		launch   = "launch a new service",
		port     = "list all port",
		inject   = "inject address luascript.lua",
		portkill = "kill port, exp: portkill addr",
	}
	local str = string.format("%-8s    %s\n", "cmd", "description")
	str = str .. "--------------------------------------------------\n"
	for k, v in pairs(h) do
		str = str .. string.format("%-8s    %s\n", k, v)
	end
	str = str .. "--------------------------------------------------"
	return str
end
function CMD.list( ... )
	local str = string.format("%-10s    %s\n", "handle", "param")
	str = str .. "--------------------------------------------------\n"
	local service_list = {system.call(1, "json", "online")}
	for i, v in pairs(service_list) do
		str = str .. string.format("%010d    %s\n", v.handle, v.param)
	end
	str = str .. "--------------------------------------------------"
	return str
end

function CMD.exit( ... )
	agent:close()
	system.exit()
end

function CMD.kill(addr)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	local ok, ret = pcall(system.kill, handle)
	if ok then
		return "kill ok"
	else
		return ret
	end
end

function CMD.info(addr)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	if handle == system.self() then
		return format_table(trace.info())
	end
	local ok1, service_info1 = pcall(system.call, 1, "json", "info", handle)
	local ok2, service_info2 = pcall(system.call, handle, "trace", "info")
	if not ok2 then
		return service_info2
	end
	for k, v in pairs(service_info2) do
		service_info1[k] = v
	end
	return format_table(service_info1)
end

function CMD.gc(addr)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	if handle == system.self() then
		return format_table(trace.gc())
	end
	
	local ok, service_info = pcall(system.call, handle, "trace", "gc")
	if not ok then
		return service_info
	end
	return format_table(service_info)
end

function CMD.profile(addr, on)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end

	if on ~= "true" and on ~= "false" then
		return "cmd error: usage => profile addr true"
	end
	local open = false
	if on == "true" then
		open = true
	end
	if handle == system.self() then
		return trace.profile(open)
	end

	local ok, ret = pcall(system.call, handle, "trace", "profile", open)
	return ret
end

function CMD.ps()
	local service_info = { }

	local service_list = {system.call(1, "json", "online")}
	for i, v in pairs(service_list) do
		if v.handle == system.self() then
			local m = trace.info()
			m.handle = v.handle
			m.param  = v.param
			table.insert(service_info, m)
		elseif v.handle > 2 then
			local ok, info = pcall(system.call, v.handle, "trace", "info")
			if ok then
				info.handle = v.handle
				info.param  = v.param
				table.insert(service_info, info)
			end
		end
	end
	local str = string.format("%-10s  %-13s %-6s %-6s\n", "handle", "cpu", "mem", "param")
	for j, service in pairs(service_info) do
		str = str .. string.format("%010d  %-13d %-6s %-6s\n", service.handle, service.cpu, service.mem, service.param)
	end
	return str:sub(1, -2)
end

function CMD.ping(addr)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	if handle == system.self() then
		return "0";
	end
	
	local a = system.now().micros()
	local ok, ret = pcall(system.call, handle, "trace", "ping")
	if not ok then
		return ret
	end
	local b = system.now().micros()
	return tostring(b-a)
end

function CMD.call(...)
	local args = format_args(...)
	local rets = {pcall(system.call, table.unpack(args))}
	local ok = table.remove(rets, 1)
	local str = ""
	for i, v in pairs(rets) do
		if type(v) == "table" then
			str = str .. json.encode(v) .. " "
		else
			str = str .. tostring(v) .. " "
		end
	end
	return str
end

function CMD.launch(...)
	local args = format_args(...)
	return CMD.call(1, "json", "launch", table.unpack(args))
end


function CMD.port(addr)
	if not addr then
		local list = system.port_list()
		local str = string.format("%-10s    %10s\n", "owner(handle)", "port(id)")
		for id, handle in pairs(list) do
			str = str .. string.format("%010d         %010d\n", handle, id)
		end
		return str:sub(1, -2);
	end
	local id = tonumber(addr)
	if not id then
		return "port addr error"
	end
	local name = system.port_name(id)
	if not name or #name == 0 then
		return "port not found"
	end
	if name:lower() == "sig" then
		return "signal: " .. signal.info()
	end
	if name:lower() == "tcplistener" then
		local t = socket.tcp.wrap(id)
		local ok, rets = pcall(t.info, t)
		rets.type = "tcp listener"
		return format_table(rets)
	end
	if name:lower() == "tcpconnection" then
		local t = socket.tcp.wrap(id)
		local ok, rets = pcall(t.info, t)
		rets.type = "tcp connection"
		return format_table(rets)
	end
	if name:lower() == "udp" then
		local t = socket.udp.wrap(id)
		local ok, rets = pcall(t.info, t)
		rets.type = "udp socket"
		return format_table(rets)
	end
	if name:lower() == "exe" then
		local t = socket.exe.wrap(id)
		local ok, rets = pcall(t.info, t)
		rets.type = "unix domain socket"
		return format_table(rets)
	end

	return "error"
end

function CMD.inject(addr, filename)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	if handle == system.self() then
		return "can't inject console(self)"
	end
	local rets = {pcall(system.call, handle, "trace", "inject", filename)}
	local ok = table.remove(rets, 1)
	local str = ""
	for i, v in pairs(rets) do
		if type(v) == "table" then
			str = str .. json.encode(v) .. ","
		else
			str = str .. tostring(v) .. ","
		end
	end
	return str:sub(1, #str-1)
end

function CMD.debug(addr, cmd, ...)
	local handle = format_addr(addr)
	if not handle then
		return "cmd error: addr error"
	end
	if handle == system.self() then
		return "can't debug console(self)"
	end

	local args = format_args(...)
	local rets = {pcall(debug[cmd], handle, table.unpack(args))}
	local ok = table.remove(rets, 1)
	if not ok then
		return json.encode(rets)
	end
	if type(rets) ~= "table" then
		return tostring(rets)
	end
	local str = ""
	for i, v in pairs(rets) do
		if type(v) == "table" then
			for key, value in pairs(v) do
				str = str .. string.format("%10s: %s\n", tostring(key), tostring(value))
			end
		else 
			str = str .. tostring(v) .. " "
		end
	end
	return str
end

function CMD.portkill(addr)
	local id = tonumber(addr)
	if not id then
		return "port addr error"
	end
	local list = system.port_list()
	local handle = list[id]
	if not handle then
		return "port not found"
	end
	local ok = pcall(system.cast, 1, "json", "portkill", handle, id)
	return ""
end

function CMD.shutdown()
	system.abort()
end





local last_time = os.time()
local prefix = ">"
local cache = ""
local cmdlist = { }
agent:start({active=true})
agent:write("Welcome to console " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
agent:write(prefix)

local function unpack(chunk)
	local cmdlist = { }
	while true do
		local lb, le = chunk:find(".-\r?\n", 1, false)
		if not lb then
			return cmdlist, chunk
		end
		local cmd = chunk:sub(lb, le)
		table.insert(cmdlist, cmd)
		chunk = chunk:sub(le+1)
	end
end
local function exec(f, ...)
	local result
	if CMD[f] then
		result = CMD[f](...)
		return result
	else
		return "command not found"
	end
end
local function docmd(cmdlist)
	for i, cmdline in ipairs(cmdlist) do
		local split = {}
		for i in string.gmatch(cmdline, "%S+") do
			table.insert(split, i)
		end
		local result = exec(table.unpack(split))
		agent:write(result .. "\n")
		agent:write(prefix)
	end
end
while true do
	system.receive {
		[{"tcp"}] = function(type, subtype, id, session, msg)
			if #msg == 0 then
				system.exit()
			end
			cache = cache .. msg
			cmdlist, cache = unpack(cache)
			docmd(cmdlist)
			last_time = os.time()
		end,
		[{"json"}] = function(type, source, filename, fileline, msg)
			agent:write("\n")
			agent:write(msg .. "\n")
			agent:write("service("..tostring(source) .. ") ".. tostring(filename) .. ":" ..tostring(fileline) .. " break point\n>")
			last_time = os.time()
		end,
		[{"after", 60}] = function()
			local curr_time = os.time()
			local cp = curr_time - last_time
			if cp >= 120 then
				system.exit()
			end
		end,
	}
end
