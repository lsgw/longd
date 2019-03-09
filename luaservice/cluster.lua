local system  = require "system"
local cluster = { }

function cluster.open(localaddr, localname, localhide, remoteaddr)
	if cluster.handle then
		return cluster.handle
	end

	cluster.handle = system.query("clusterd")
	if cluster.handle then
		return cluster.handle
	end
	local ip, port = localaddr:match("^(%d+%.%d+%.%d+%.%d+):(%d+)$")
	assert(ip and port)

	cluster.handle = system.newservice("clusterd", localaddr, localname, localhide, remoteaddr)
	assert(cluster.handle, "launch clusterd fail!")
	system.register(cluster.handle, "clusterd")

	return cluster.handle
end

function cluster.query(node, name)
	return system.call(cluster.handle, "lua", "query", node, name)
end
function cluster.call(node, handle, type, ...)
	return system.call(cluster.handle, "lua", "call", node, handle, type, ...)
end
function cluster.cast(node, handle, type, ... )
	return system.cast(cluster.handle, "lua", "cast", node, handle, type, ...)
end
function cluster.timeout(node, s)
	return system.cast(cluster.handle, "lua", "timeout", node, s)
end
function cluster.alias(node, handle)
	return system.call(cluster.handle, "lua", "alias", node, handle)
end
function cluster.nodes()
	return system.call(cluster.handle, "lua", "nodes")
end
function cluster.node(name)
	return system.call(cluster.handle, "lua", "node", name)
end


return cluster