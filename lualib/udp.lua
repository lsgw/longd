local system = require "system"
local api    = require "api"
local codec  = require "api.udp.codec"

local udp    = { type = {
    kOpen   = 1,
    kSend   = 2,
    kRecv   = 3,
    kClose  = 4,
    kOpts   = 5,
    kStatus = 6,
    kInfo   = 7,
}}

do
    system.protocol.register({
        name   = "udp",
        id     = system.protocol.MSG_TYPE_UDP,
        pack   = codec.encode_to_lightuserdata,
        unpack = codec.decode_from_lightuserdata,
    })
end

function udp.open(addr)
    assert(type(addr) == "table", "udp open addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    local ip = addr.ip or "0.0.0.0"
    local port = addr.port or 0
    local ipv6 = addr.ipv6 or false
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("open", 0, session, ip, port, ipv6)
    local id, err = api.newport("udp", system.protocol.MSG_TYPE_UDP, udata, nbyte)
    assert(id, tostring(err))
    return system.receive {
        [{"udp", udp.type.kStatus, id, session}] = function(type, subtype, id, session, online)
            assert(online)
            return id;
        end
    }
end

-- opts = {reuseaddr=true, reuseport=true, active=true, owner=system.self(), read=true}
function udp.setopts(id, opts)
    assert(type(id) == "number" and type(opts)=="table", "param error")
    local udata, nbyte = codec.encode_to_lightuserdata("opts", id, 0, opts)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_UDP, udata, nbyte)
    assert(succ, tostring(err))
end

function udp.start(id, opts)
    opts = opts or { }
    opts.owner = system.self()
    opts.read  = true
    udp.setopts(id, opts)
end

function udp.sendto(id, addr, data, size)
    assert(type(id) == "number", "udp recvto id must be an integer")
    assert(type(addr) == "table", "udp addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    assert(type(addr.ip) == "string", "udp addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    assert(type(addr.port) == "number", "udp addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    assert(type(addr.ipv6) == "boolean" or type(addr.ipv6) == "nil", "udp addr must be an table. example : {ip=127.0.0.1,port=8085,ipv6=false}")
    
    local ip = addr.ip
    local port = addr.port
    local ipv6 = addr.ipv6 or false
    local udata, nbyte = codec.encode_to_lightuserdata("send", id, 0, ip, port, ipv6, data, size)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_UDP, udata, nbyte)
    assert(succ, tostring(err))
end

function udp.recvfrom(id)
    assert(type(id) == "number", "udp recvfrom id must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("recv", id, session)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_UDP, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"udp", udp.type.kRecv, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

function udp.recvfrom_timeout(id, timeout)
    assert(type(id) == "number" and type(timeout) == "number")
    udp.setopts(id, {active=true})
    return system.receive {
        [{"udp", udp.type.kRecv, id, 0}] = function(type, subtype, id, session, ...)
            udp.setopts(id, {active=false})
            return ...
        end,
        [{"after", timeout}] = function()
            udp.setopts(id, {active=false})
            return nil
        end
    }
end

function udp.close(id)
    assert(type(id) == "number", "close id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("close", id, 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_UDP, udata, nbyte)
    assert(succ, tostring(err))
end

function udp.info(id)
    assert(type(id) == "number", "udp recvfrom id must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("info", id, session)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_UDP, udata, nbyte)
    return system.receive {
        [{"udp", udp.type.kInfo, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

return udp