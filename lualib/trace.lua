local api    = require "api"
local trace  = { }

function trace.info()
	local value = collectgarbage("count")
	local i = 1
	while value >= 1024 and i < 4 do
		value = value / 1024
		i = i + 1
	end
	local suffix = {"KB", "MB", "GB", "TB"}

	local m = {
		mem     = tostring(math.ceil(value)) .. suffix[i],
		cpu     = api.cpu(),
		recive  = api.msize(),
		pending = api.psize(),
		profile = api.get_profile(),
		handle  = string.format("%010d", api.handle()),
	}
	return m
end

function trace.gc()
	collectgarbage("collect")
	return trace.info()
end

function trace.profile(on)
	api.set_profile(on)
	local p = api.get_profile()
	if p == on then
		return "ok"
	else
		return "fail"
	end
end
function trace.ping()
	return "ok"
end

function trace.inject(filename)
	local fp  = io.open(filename, "r")
	if not fp then
		return
	end
	local str = fp:read("*all")
	fp:close()
	if filename then
		filename = "@" .. filename
	else
		filename = "=(load)"
	end

	local output = {}
	local function print(...)
		local value = { }
		for k,v in ipairs({...}) do
			value[k] = tostring(v)
		end
		table.insert(output, table.concat(value, "\t"))
	end
	local env = setmetatable( { print = print }, { __index = _ENV })
	local fun, err = load(str, filename, "bt", env)
	if not fun then
		return err
	end
	local rets = {xpcall(fun, debug.traceback)}
	local ok = table.remove(rets, 1)
	table.insert(output, table.concat(rets, ","))
	return table.concat(output, "\n")
end

return trace