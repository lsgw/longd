local system = require "system"
local api    = require "api"
local codec  = require "api.tcp.codec"

local tcp    = { type = {
    kListen        =  1,
    kAccept        =  2,
    kConnect       =  3,
    kRead          =  4,
    kWrite         =  5,
    kShutdown      =  6,
    kClose         =  7,
    kOpts          =  8,
    kStatus        =  9,
    kShift         = 10,
    kLowWaterMark  = 11,
    kHighWaterMark = 12,
    kInfo          = 13,
}}

do
    system.protocol.register({
        name     = "tcp",
        id       = system.protocol.MSG_TYPE_TCP,
        pack     = codec.encode_to_lightuserdata,
        unpack   = codec.decode_from_lightuserdata,
    })
end

function tcp.listen(addr)
    assert(type(addr) == "table", "tcp listen addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    local ip = addr.ip or "0.0.0.0"
    local port = addr.port or 0
	local ipv6 = addr.ipv6 or false

    local session = api.newsession()
	local udata, nbyte = codec.encode_to_lightuserdata("listen", 0, session, ip, port, ipv6)
    local id, err = api.newport("TcpListener", system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(id, tostring(err))
    return system.receive {
        [{"tcp", tcp.type.kStatus, id, session}] = function(type, subtype, id, session, online)
            assert(online)
            return id;
        end
    }
end
function tcp.connect(addr)
    assert(type(addr) == "table", "tcp connect addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    local ip = addr.ip or "0.0.0.0"
    local port = addr.port or 0
    local ipv6 = addr.ipv6 or false

    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("connect", 0, session, ip, port, ipv6)
    local id, err = api.newport("TcpClient", system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(id, tostring(err))
    return system.receive {
        [{"tcp", tcp.type.kStatus, id, session}] = function(type, subtype, id, session, online, conn)
            if online then
                return conn
            else
                return nil
            end
        end
    }
end


-- opts = {reuseaddr=true, reuseport=true, keepalive=true, nodelay=true, active=true, owner=system.self(), read=true}
function tcp.setopts(id, opts)
    assert(type(id) == "number" and type(opts)=="table", "param error")
    local udata, nbyte = codec.encode_to_lightuserdata("opts", id, 0, opts)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.start(id, opts)
    opts = opts or { }
    opts.owner = system.self()
    opts.read  = true
    tcp.setopts(id, opts)
end


function tcp.accept(id)
    assert(type(id) == "number", "tcp accept id must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("accept", id, session)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"tcp", tcp.type.kAccept, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

function tcp.read(id, size)
    assert(type(id) == "number", "read id must be an integer")
    assert(type(size) == "number" or type(size) == "nil", "read size must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("read", id, session, size or 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"tcp", tcp.type.kRead, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

function tcp.read_timeout(id, timeout)
    assert(type(id) == "number" and type(timeout) == "number")
    tcp.setopts(id, {active=true})
    return system.receive {
        [{"tcp", tcp.type.kRead, id, 0}] = function(type, subtype, id, session, ...)
            tcp.setopts(id, {active=false})
            return ...
        end,
        [{"after", timeout}] = function()
            tcp.setopts(id, {active=false})
            return nil
        end
    }
end

function tcp.write(id, data, size)
    assert(type(id) == "number", "tcp write id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("write", id, 0, data, size)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.shutdown(id)
    assert(type(id) == "number", "close id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("shutdown", id, 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.close(id)
    assert(type(id) == "number", "close id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("close", id, 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.low_water_mark(id, on, value)
    assert(type(id) == "number" and type(on) == "boolean")
    local udata, nbyte = codec.encode_to_lightuserdata("low_water_mark", id, 0, on, value or 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.high_water_mark(id, on, value)
    assert(type(id) == "number" and type(on) == "boolean")
    local udata, nbyte = codec.encode_to_lightuserdata("high_water_mark", id, 0, on, value or 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
end

function tcp.info(id)
    assert(type(id) == "number", "read id must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("info", id, session)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_TCP, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"tcp", tcp.type.kInfo, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

return tcp

