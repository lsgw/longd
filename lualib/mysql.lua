local tcp   = require "tcp"
local crypt = require "crypt"


local sub = string.sub
local strgsub = string.gsub
local strformat = string.format
local strbyte = string.byte
local strchar = string.char
local strrep = string.rep
local strunpack = string.unpack
local strpack = string.pack
local sha1= crypt.sha1
local setmetatable = setmetatable
local error = error
local tonumber = tonumber
local new_tab = function (narr, nrec) return {} end


local STATE_CONNECTED = 1
local STATE_COMMAND_SENT = 2

local COM_QUERY = 0x03

local SERVER_MORE_RESULTS_EXISTS = 8

-- 16MB - 1, the default max allowed packet size used by libmysqlclient
local FULL_PACKET_SIZE = 16777215

-- mysql field value type converters
local converters = new_tab(0, 8)

for i = 0x01, 0x05 do
    -- tiny, short, long, float, double
    converters[i] = tonumber
end
converters[0x08] = tonumber  -- long long
converters[0x09] = tonumber  -- int24
converters[0x0d] = tonumber  -- year
converters[0xf6] = tonumber  -- newdecimal




local function _get_byte2(data, i)
    return strunpack("<I2",data,i)
end


local function _get_byte3(data, i)
    return strunpack("<I3",data,i)
end


local function _get_byte4(data, i)
    return strunpack("<I4",data,i)
end


local function _get_byte8(data, i)
    return strunpack("<I8",data,i)
end


local function _set_byte2(n)
    return strpack("<I2", n)
end


local function _set_byte3(n)
    return strpack("<I3", n)
end


local function _set_byte4(n)
    return strpack("<I4", n)
end


local function _from_cstring(data, i)
    return strunpack("z", data, i)
end

local function _dumphex(bytes)
    return strgsub(bytes, ".", function(x) return strformat("%02x ", strbyte(x)) end)
end

local function _from_length_coded_bin(data, pos)
    local first = strbyte(data, pos)

    if not first then
        return nil, pos
    end

    if first >= 0 and first <= 250 then
        return first, pos + 1
    end

    if first == 251 then
        return nil, pos + 1
    end

    if first == 252 then
        pos = pos + 1
        return _get_byte2(data, pos)
    end

    if first == 253 then
        pos = pos + 1
        return _get_byte3(data, pos)
    end

    if first == 254 then
        pos = pos + 1
        return _get_byte8(data, pos)
    end

    return false, pos + 1
end

local function _from_length_coded_str(data, pos)
    local len
    len, pos = _from_length_coded_bin(data, pos)
    if len == nil then
        return nil, pos
    end

    return sub(data, pos, pos + len - 1), pos + len
end







local function _parse_ok_packet(packet)
    local ok = { }
    local pos

    ok.affected_rows, pos = _from_length_coded_bin(packet, 2)

    ok.insert_id, pos = _from_length_coded_bin(packet, pos)

    ok.server_status, pos = _get_byte2(packet, pos)

    ok.warning_count, pos = _get_byte2(packet, pos)


    local message = sub(packet, pos)
    if message and message ~= "" then
        ok.message = message
    end


    return ok
end


local function _parse_eof_packet(packet)
    local eof = { }
    local pos = 2

    eof.warning_count, pos = _get_byte2(packet, pos)
    eof.status_flags = _get_byte2(packet, pos)

    return eof
end


local function _parse_err_packet(packet)
    local err = { }
    local pos = 2
    err.errno, pos = _get_byte2(packet, pos)
    err.marker = sub(packet, pos, pos)
    
    if err.marker == '#' then
        -- with sqlstate
        pos = pos + 1
        err.sqlstate = sub(packet, pos, pos + 5 - 1)
        pos = pos + 5
    end

    err.message = sub(packet, pos)
    return err
end


local function _parse_result_set_header_packet(packet)
    local field = { }
    local pos = 1

    field.count, pos = _from_length_coded_bin(packet, pos)
    field.extra = _from_length_coded_bin(packet, pos)
    return field
end


local function _parse_field_packet(data)
    local col = { }
    local catalog, db, table, orig_table, orig_name, charsetnr, length
    local pos
    catalog, pos = _from_length_coded_str(data, 1)


    db, pos = _from_length_coded_str(data, pos)
    table, pos = _from_length_coded_str(data, pos)
    orig_table, pos = _from_length_coded_str(data, pos)
    col.name, pos = _from_length_coded_str(data, pos)

    orig_name, pos = _from_length_coded_str(data, pos)

    pos = pos + 1 -- ignore the filler

    charsetnr, pos = _get_byte2(data, pos)

    length, pos = _get_byte4(data, pos)

    col.type = strbyte(data, pos)

    return col
end


local function _parse_row_data_packet(data, cols, compact)
    local pos = 1
    local ncols = #cols
    local row
    if compact then
        row = new_tab(ncols, 0)
    else
        row = new_tab(0, ncols)
    end
    for i = 1, ncols do
        local value
        value, pos = _from_length_coded_str(data, pos)
        local col = cols[i]
        local typ = col.type
        local name = col.name

        if value ~= nil then
            local conv = converters[typ]
            if conv then
                value = conv(value)
            end
        end

        if compact then
            row[i] = value

        else
            row[name] = value
        end
    end

    return row
end


local function _compute_token(password, scramble)
    if password == "" then
        return ""
    end
    --_dumphex(scramble)

    local stage1 = sha1(password)
    --print("stage1:", _dumphex(stage1) )
    local stage2 = sha1(stage1)
    local stage3 = sha1(scramble .. stage2)

    local i = 0
    return strgsub(stage3,".",
        function(x)
            i = i + 1
            -- ~ is xor in lua 5.3
            return strchar(strbyte(x) ~ strbyte(stage1, i))
        end)
end


local function _compose_packet(packet_no, req)
    local size = #req
    local packet = _set_byte3(size) .. strchar(packet_no) .. req
    return packet
end

local function _compose_query(packet_no, query)
    local cmd_packet = strchar(COM_QUERY) .. query
    local querypacket = _compose_packet(packet_no, cmd_packet)
    return querypacket
end






local function _get_packet(cache, max_packet_size)
    local ok, packet_header = pcall(string.unpack, "c4", cache, 1)
    if not ok then
        return nil, nil, "failed to receive packet header: " .. packet_header
    end

    local len, pos = _get_byte3(packet_header, 1)
    if len == 0 then
        return nil, nil, "empty packet"
    end

    assert(len<=max_packet_size, "packet size too big: " .. len)
    
    local packet_no = strbyte(packet_header, pos)
    local packet_data = string.sub(cache, 5, 5+len-1)
    local remain = string.sub(cache, 5+len)
    
    if not packet_data or #packet_data ~= len then
        return nil, nil, "failed to read packet content: "
    end

    local field_count = strbyte(packet_data, 1)
    local packet_type
    
    if field_count == 0x00 then
        packet_type = "OK"
    elseif field_count == 0xff then
        packet_type = "ERR"
    elseif field_count == 0xfe then
        packet_type = "EOF"
    else
        packet_type = "DATA"
    end

    return packet_type, packet_no, packet_data, remain
end


local function _mysql_login(handshake_packet, handshake_no, database, user, password, max_packet_size)

    local aat = {}
    aat.protocol_ver = strbyte(handshake_packet)

    local server_ver, pos = _from_cstring(handshake_packet, 2)
    if not server_ver then
        error "bad handshake initialization handshake_packet: bad server version"
    end
    aat.server_ver = server_ver


    local thread_id, pos = _get_byte4(handshake_packet, pos)

    local scramble1 = sub(handshake_packet, pos, pos + 8 - 1)
    if not scramble1 then
        error "1st part of scramble not found"
    end

    pos = pos + 9 -- skip filler

    -- two lower bytes
    aat.server_capabilities, pos = _get_byte2(handshake_packet, pos)

    aat.server_lang = strbyte(handshake_packet, pos)
    pos = pos + 1

    aat.server_status, pos = _get_byte2(handshake_packet, pos)

    local more_capabilities, pos = _get_byte2(handshake_packet, pos)

    aat.server_capabilities = aat.server_capabilities|more_capabilities<<16

    local len = 21 - 8 - 1

    pos = pos + 1 + 10

    local scramble_part2 = sub(handshake_packet, pos, pos + len - 1)
    if not scramble_part2 then
        error "2nd part of scramble not found"
    end


    local scramble = scramble1..scramble_part2
    local token = _compute_token(password, scramble)

    local client_flags = 260047;

    local req = strpack("<I4I4c24zs1z",
        client_flags,
        max_packet_size,
        strrep("\0", 24),   -- TODO: add support for charset encoding
        user,
        token,
        database)

    local auth_packet=_compose_packet(handshake_no+1, req)

    return auth_packet, aat
end

local function _auth_resp(typ, no, packet)
    if not typ then
        --print("recv auth resp : failed to receive the result packet")
        error ("failed to receive the result packet"..packet)
        -- return nil, packet
    end

    if typ == 'ERR' then
        local err = _parse_err_packet(packet)
        error( strformat("errno:%d, msg:%s,sqlstate:%s",err.errno,err.msg,err.sqlstate))
        --return nil, errno,msg, sqlstate
    end

    if typ == 'EOF' then
        error "old pre-4.1 authentication protocol not supported"
    end

    if typ ~= 'OK' then
        error "bad packet type: "
    end
    return true, true
end




local function get_result(cache, max_packet_size)
    local typ, no, packet, remain = _get_packet(cache, max_packet_size)
    if not typ then
        return nil, cache, packet
    end
    if typ == "ERR" then
        local err = _parse_err_packet(packet)
        return err, remain
    end

    if typ == 'OK' then
        local ok = _parse_ok_packet(packet)
        if ok and ok.server_status&SERVER_MORE_RESULTS_EXISTS ~= 0 then
            return ok, remain, "again"
        else
            return ok, remain
        end
    end
    assert(typ=="DATA", "packet type " .. typ .. " not supported")

    local field = _parse_result_set_header_packet(packet)

    local cols = new_tab(field_count, 0)
    for i = 1, field.count do
        typ, no, packet, remain = _get_packet(remain, max_packet_size)
        if not typ then
            return nil, cache, packet
        end
        assert(typ=="DATA", "bad field packet type: " .. typ)
        local col = _parse_field_packet(packet)
        cols[i] = col
    end

    local typ, no, packet, remain = _get_packet(remain, max_packet_size)
    if not typ then
        return nil, cache, packet
    end
    assert(typ=="EOF", "unexpected packet type " .. typ .. " while eof packet is ".. "expected")


    local rows = { }
    local i = 0
    while true do
        typ, no, packet, remain = _get_packet(remain, max_packet_size)
        if not typ then
            return nil, cache, packet
        end

        if typ == 'EOF' then
            local eof = _parse_eof_packet(packet)
            if eof.status_flags & SERVER_MORE_RESULTS_EXISTS ~= 0 then
                return rows, remain, "again"
            end
            break
        end

        local row = _parse_row_data_packet(packet, cols, compact)
        i = i + 1
        rows[i] = row
    end

    return rows
end

------------------------------------------------------------------
local _M = { _VERSION = '0.13' }

function _M.connect(opts)
	local self = setmetatable({}, { __index = _M })

    local max_packet_size = opts.max_packet_size
    if not max_packet_size then
        max_packet_size = 1024 * 1024 -- default 1 MB
    end
    self.max_packet_size = max_packet_size
    self.compact = opts.compact_arrays
    self.cache = ""


    local database = opts.database or ""
    local user = opts.user or ""
    local password = opts.password or ""

	local conn, addr = tcp.connect({ip=opts.host, port=opts.port or 3306, ipv6=opts.ipv6 or false})
	assert(conn>0)
	tcp.start(conn, {active=false, keepalive=true, nodelay=true})
    self.conn = conn
	self.cache = tcp.read(conn)
    local typ, handshake_no, handshake_packet, remain = _get_packet(self.cache, self.max_packet_size)

    assert(typ, handshake_packet)
    if typ == "ERR" then
        local res = _parse_err_packet(handshake_packet)
        error( strformat("errno:%d, msg:%s,sqlstate:%s",res.errno,res.msg,res.sqlstate))
    end

    local auth_packet, aat = _mysql_login(handshake_packet, handshake_no, database, user, password, max_packet_size)
    tcp.write(conn, auth_packet)

    local packet, no
    self.cache = tcp.read(conn)
    typ, no, packet, remain = _get_packet(self.cache, self.max_packet_size)

    local ok, err = _auth_resp(typ, no, packet)
   
    if ok then
        return self
    else
        tcp.close(self.conn)
        return nil
    end
end


function _M.query(self, sql)
    self.packet_no = 0
    self.cache = ""
    tcp.write(self.conn, _compose_query(self.packet_no, sql))

    local rows, remain, err
    while not rows do
        local msg = tcp.read(self.conn)
        if #msg == 0 then
            return "mysql connection close"
        end
        self.cache = self.cache .. msg
        rows, remain, err = get_result(self.cache, self.max_packet_size)
        self.cache = remain
    end
    if err ~= "again" then
        return rows
    end


    local result = { rows }
    result.mulit = true
    local i = 2
    while not rows or err == "again" do
        local msg = socket.read(self.conn)
        if #msg == 0 then
            return "mysql connection close"
        end
        self.cache = self.cache .. msg
        rows, remain, err = get_result(self.cache, self.max_packet_size)
        self.cache = remain
        result[i] = rows
        i = i + 1
    end
    return result
end


function _M.close(self)
    tcp.close(self.conn)
    setmetatable(self, nil)
end

return _M