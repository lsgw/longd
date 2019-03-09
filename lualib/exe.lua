local system = require "system"
local api    = require "api"
local codec  = require "api.exe.codec"

local exe    = { type = {
    kOpen   = 1,
    kWrite  = 2,
    kRead   = 3,
    kClose  = 4,
    kStatus = 5,
    kOpts   = 6,
    kInfo   = 7,
}}

do
    system.protocol.register({
        name   = "exe",
        id     = system.protocol.MSG_TYPE_EXE,
        pack   = codec.encode_to_lightuserdata,
        unpack = codec.decode_from_lightuserdata,
    })
end

function exe.open(pathname, ...)
    assert(type(pathname) == "string", "pathname must be an string")
    local argv = ""
    for i, value in ipairs({...}) do
        argv = argv .. " " .. string.gsub(tostring(value), "^%s*(.-)%s*$", "%1")
    end
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("open", 0, session, pathname .. argv)
    local id, err = api.newport("exe", system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(id, tostring(err))
    return system.receive {
        [{"exe", exe.type.kStatus, id, session}] = function(type, subtype, id, session, online, ...)
            if online then
                return id, ...
            else
                return nil
            end
        end
    }
end

function exe.write(id, data, size)
    assert(type(id) == "number", "tcp write id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("write", id, 0, data, size)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(succ, tostring(err))
end

function exe.read(id, size)
	assert(type(id) == "number", "read id must be an integer")
    assert(type(size) == "number" or type(size) == "nil", "read size must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("read", id, session, size or 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"exe", exe.type.kRead, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end

function exe.read_timeout(id, timeout)
    assert(type(id) == "number" and type(timeout) == "number")
    exe.setopts(id, {active=true})
    return system.receive {
        [{"exe", exe.type.kRecv, id, 0}] = function(type, subtype, id, session, ...)
            exe.setopts(id, {active=false})
            return ...
        end,
        [{"after", timeout}] = function()
            exe.setopts(id, {active=false})
            return nil
        end
    }
end

-- opts = {active=true, owner=system.self(), read=true}
function exe.setopts(id, opts)
    assert(type(id) == "number" and type(opts)=="table", "param error")
    local udata, nbyte = codec.encode_to_lightuserdata("opts", id, 0, opts)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(succ, tostring(err))
end

function exe.start(id, opts)
    opts = opts or { }
    opts.owner = system.self()
    opts.read  = true
    exe.setopts(id, opts)
end

function exe.close(id)
    assert(type(id) == "number", "close id must be an integer")
    local udata, nbyte = codec.encode_to_lightuserdata("close", id, 0)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(succ, tostring(err))
end

function exe.info(id)
    assert(type(id) == "number", "read id must be an integer")
    local session = api.newsession()
    local udata, nbyte = codec.encode_to_lightuserdata("info", id, session)
    local succ, err = api.command(id, system.protocol.MSG_TYPE_EXE, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"exe", exe.type.kInfo, id, session}] = function(type, subtype, id, session, ...)
            return ...
        end
    }
end


return exe
