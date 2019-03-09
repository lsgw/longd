local udp = require "udp"
local tcp = require "tcp"
local exe = require "exe"

local socket = { udp = { }, tcp = { }, exe = { } }

--- wrap udp
local _UDP_M = { _VERSION = '5.34' }
function _UDP_M.setopts(self, opts)
    udp.setopts(self.id, opts)
end
function _UDP_M.start(self, opts)
    udp.start(self.id, opts)
end
function _UDP_M.send(self, addr, data, size)
    udp.sendto(self.id, addr, data, size)
end
function _UDP_M.recv(self)
    return udp.recvfrom(self.id)
end
function _UDP_M.close(self)
    udp.close(self.id)
    setmetatable(self, nil)
end
function _UDP_M.info(self)
    return udp.info(self.id)
end
function _UDP_M.unwrap(self)
    return self.id
end

function socket.udp.open(addr)
    local id = udp.open(addr)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _UDP_M })
    self.id = id
    return self
end
function socket.udp.wrap(id)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _UDP_M })
    self.id = id
    return self
end


--- wrap tcp
local _TCP_M = { _VERSION = '5.34' }
function _TCP_M.setopts(self, opts)
    tcp.setopts(self.id, opts)
end
function _TCP_M.start(self, opts)
    tcp.start(self.id, opts)
end
function _TCP_M.accept(self)
    local id, addr = tcp.accept(self.id)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _TCP_M })
    self.id   = id
    self.addr = addr
    return self
end
function _TCP_M.read(self, size)
    return tcp.read(self.id, size)
end
function _TCP_M.write(self, data, size)
    tcp.write(self.id, data, size)
end
function _TCP_M.shutdown(self)
    tcp.shutdown(self.id)
end
function _TCP_M.close(self)
    tcp.close(self.id)
    setmetatable(self, nil)
end

function _TCP_M.low_water_mark(self, on, value)
    tcp.low_water_mark(self.id, on, value)
end

function _TCP_M.high_water_mark(self, on, value)
    tcp.high_water_mark(self.id, on, value)
end
function _TCP_M.info(self)
    return tcp.info(self.id)
end

function _TCP_M.unwrap(self)
    if self.addr then
        return self.id, self.addr
    else
        return self.id
    end
end

function socket.tcp.listen(addr)
    local id = tcp.listen(addr)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _TCP_M })
    self.id = id
    self.addr = addr
    return self
end
function socket.tcp.connect(addr)
    local id = tcp.connect(addr)
    if id then
        local self = setmetatable({}, { __index = _TCP_M })
        self.id = id
        self.addr = addr
        return self
    else
        return nil
    end
end
function socket.tcp.wrap(id, addr)
    assert(type(id) == "number")
    assert(type(addr) == "table" or type(addr) == "nil")
    local self = setmetatable({}, { __index = _TCP_M })
    self.id = id
    self.addr = addr
    return self
end








local _EXE_M = { _VERSION = '5.34' }
function _EXE_M.setopts(self, opts)
    exe.setopts(self.id, opts)
end
function _EXE_M.start(self, opts)
    exe.start(self.id, opts)
end
function _EXE_M.write(self, data, size)
    exe.write(self.id, data, size)
end
function _EXE_M.read(self, size)
    return exe.read(self.id, size)
end
function _EXE_M.read_timeout(self, size, timeout)
    return exe.read_timeout(self.id, size, timeout)
end
function _EXE_M.close(self)
    exe.close(self.id)
    setmetatable(self, nil)
end
function _EXE_M.info(self)
    return exe.info(self.id)
end

function _EXE_M.unwrap(self)
    if self.pid then
        return self.id, self.pid
    else
        return self.id
    end
end

function socket.exe.open(pathname, ...)
    local id, pid = exe.open(pathname, ...)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _EXE_M })
    self.id = id
    self.pid = pid
    return self
end
function socket.exe.wrap(id)
    assert(type(id) == "number")
    local self = setmetatable({}, { __index = _EXE_M })
    self.id = id
    return self
end

return socket