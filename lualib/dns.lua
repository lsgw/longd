-- [[
-- lua dns resolver library
-- See  https://github.com/xjdrew/levent/blob/master/levent/dns.lua for more detail
-- ]]

--[[
-- author: xjdrew
-- date: 2014-08-06
-- lua dns resolver library
-- conform to rfc1035 (http://tools.ietf.org/html/rfc1035)
-- support ipv6, conform to rfc1886(http://tools.ietf.org/html/rfc1886), rfc2874(http://tools.ietf.org/html/rfc2874)
--]]
-- [[
-- resource record type:
-- TYPE            value and meaning
-- A               1 a host address
-- NS              2 an authoritative name server
-- MD              3 a mail destination (Obsolete - use MX)
-- MF              4 a mail forwarder (Obsolete - use MX)
-- CNAME           5 the canonical name for an alias
-- SOA             6 marks the start of a zone of authority
-- MB              7 a mailbox domain name (EXPERIMENTAL)
-- MG              8 a mail group member (EXPERIMENTAL)
-- MR              9 a mail rename domain name (EXPERIMENTAL)
-- NULL            10 a null RR (EXPERIMENTAL)
-- WKS             11 a well known service description
-- PTR             12 a domain name pointer
-- HINFO           13 host information
-- MINFO           14 mailbox or mail list information
-- MX              15 mail exchange
-- TXT             16 text strings
-- AAAA            28 a ipv6 host address
-- only appear in the question section:
-- AXFR            252 A request for a transfer of an entire zone
-- MAILB           253 A request for mailbox-related records (MB, MG or MR)
-- MAILA           254 A request for mail agent RRs (Obsolete - see MX)
-- *               255 A request for all records
-- 
-- resource recode class:
-- IN              1 the Internet
-- CS              2 the CSNET class (Obsolete - used only for examples in some obsolete RFCs)
-- CH              3 the CHAOS class
-- HS              4 Hesiod [Dyer 87]
-- only appear in the question section:
-- *               255 any class
-- ]]

--[[
-- struct header {
--  uint16_t tid     # identifier assigned by the program that generates any kind of query.
--  uint16_t flags   # flags
--  uint16_t qdcount # the number of entries in the question section.
--  uint16_t ancount # the number of resource records in the answer section.
--  uint16_t nscount # the number of name server resource records in the authority records section.
--  uint16_t arcount # the number of resource records in the additional records section.
-- }
--
-- request body:
-- struct request {
--  string name
--  uint16_t atype
--  uint16_t class
-- }
--
-- response body:
-- struct response {
--  string name
--  uint16_t atype
--  uint16_t class
--  uint16_t ttl
--  uint16_t rdlength
--  string rdata
-- }
--]]


local udp = require "udp"

local MAX_DOMAIN_LEN = 1024
local MAX_LABEL_LEN = 63
local MAX_PACKET_LEN = 2048
local DNS_HEADER_LEN = 12

local QTYPE = {
    A = 1,
    CNAME = 5,
    AAAA = 28,
}

local QCLASS = {
    IN = 1,
}

local next_tid = 1
local function gen_tid()
    local tid = next_tid
    next_tid = next_tid + 1
    return tid
end

local function pack_header(t)
    return string.pack(">HHHHHH", t.tid, t.flags, t.qdcount, t.ancount or 0, t.nscount or 0, t.arcount or 0)
end

local function pack_question(name, qtype, qclass)
    local labels = {}
    for w in name:gmatch("([%w%-_)]+)%.?") do
        labels[#labels + 1] = string.pack("s1", w)
    end
    --labels[#labels + 1] = string.char(0)
    return string.pack(">zHH", table.concat(labels), qtype, qclass)
end

local function unpack_header(chunk)
    local tid, flags, qdcount, ancount, nscount, arcount, left = string.unpack(">HHHHHH", chunk)
    return {
        tid = tid,
        flags = flags,
        qdcount = qdcount,
        ancount = ancount,
        nscount = nscount,
        arcount = arcount
    }, left
end

-- unpack a resource name
local function unpack_name(chunk, left)
    local t = {}
    local jump_pointer
    local tag, offset, label
    while true do
        tag, left = string.unpack("B", chunk, left)
        if tag & 0xc0 == 0xc0 then
            -- pointer
            offset,left = string.unpack(">H", chunk, left - 1)
            offset = offset & 0x3fff
            if not jump_pointer then
                jump_pointer = left
            end
            -- offset is base 0, need to plus 1
            left = offset + 1
        elseif tag == 0 then
            break
        else
            label, left = string.unpack("s1", chunk, left - 1)
            t[#t+1] = label
        end
    end
    return table.concat(t, "."), jump_pointer or left
end

local function unpack_question(chunk, left)
    local name, atype, class
    name, left = unpack_name(chunk, left)
    atype, class, left = string.unpack(">HH", chunk, left)
    return {
        name = name,
        atype = atype, 
        class = class
    }, left
end

local function unpack_answer(chunk, left)
    local tag, name, atype, class, ttl, rdata
    name, left = unpack_name(chunk, left)
    atype, class, ttl, rdata, left = string.unpack(">HHI4s2", chunk, left)
    return {
        name = name,
        atype = atype,
        class = class,
        ttl = ttl,
        rdata = rdata
    },left
end

-- a 32bit internet address
local function unpack_rdata(qtype, chunk)
    if qtype == QTYPE.A then
        local a,b,c,d = string.unpack("BBBB", chunk)
        return string.format("%d.%d.%d.%d", a,b,c,d)
    elseif qtype == QTYPE.AAAA then
        local a,b,c,d,e,f,g,h = string.unpack(">HHHHHHHH", chunk)
        return string.format("%x:%x:%x:%x:%x:%x:%x:%x", a, b, c, d, e, f, g, h)
    else
        assert(nil, qtype)
    end
end

local function is_valid_hostname(name)
    if #name > MAX_DOMAIN_LEN then
        return false
    end

    if not name:match("^[%l%d-%.]+$") then
        return false
    end

    local seg
    for w in name:gmatch("([%w-]+)%.?") do
        if #w > MAX_LABEL_LEN then
            return false
        end
        seg = w
    end

    -- last segment can't be a number
    if seg:match("([%d]+)%.?") then
        return false
    end
    return true
end


-- cache
local cache = {}
function cache.query(name, ipv6)
    local qtype = ipv6 and QTYPE.AAAA or QTYPE.A
    local name = name:lower()

    local qcache = cache[qtype]
    if not qcache then
        return
    end
    local t = qcache[name]
    if t then
        if t.expired < os.time() then
            qcache[name] = nil
            return
        end
        return t.data
    end
end

function cache.add(qtype, name, data, ttl)
    if ttl and ttl > 0 and data and #data > 0 then
        local qcache = cache[qtype]
        if not qcache then
            qcache = {}
            cache[qtype] = qcache
        end
        qcache[name] = {
            expired = os.time() + ttl,
            data = data
        }
    end
end

function cache.refresh()
    local qcache = cache[QTYPE.AAAA]
    if qcache then
        for name, answer in pairs(qcache) do
            if answer.expired < os.time() then
                qcache[name] = nil
                break
            end
        end
    end

    qcache = cache[QTYPE.A]
    if qcache then
        for name, answer in pairs(qcache) do
            if answer.expired < os.time() then
                qcache[name] = nil
                break
            end
        end
    end
end



local dns = { timeout = 1, id = 0, launch = false, addr = {
    ip   = "8.8.8.0",
    port = 53,
    ipv6 = false, 
}}

function dns.pack(name, ipv6)
    assert(is_valid_hostname(name), "illegal name")
    
    local qtype = ipv6 and QTYPE.AAAA or QTYPE.A
    local name = name:lower()

    local question_header = {
        tid = gen_tid(),
        flags = 0x100, -- flags: 00000001 00000000, set RD
        qdcount = 1,
    }
    return pack_header(question_header) .. pack_question(name, qtype, QCLASS.IN), question_header.tid
end

function dns.unpack(response)
    assert(#response>DNS_HEADER_LEN, debug.traceback())

    local ok, left, answer_header, question

    answer_header,left = unpack_header(response)
    assert(answer_header.qdcount==1, debug.traceback())

    ok, question,left = xpcall(unpack_question, debug.traceback, response, left)
    assert(ok, debug.traceback())
   
    local ttl
    local answer
    local answers = {}
    for i=1, answer_header.ancount do
        ok, answer, left = xpcall(unpack_answer, debug.traceback, response, left)
        assert(ok, debug.traceback())
        -- only extract qtype address
        if answer.atype == QTYPE.AAAA or answer.atype == QTYPE.A then
            local ip
            ok, ip = xpcall(unpack_rdata, debug.traceback, answer.atype, answer.rdata)
            assert(ok, ip .. "\n" .. debug.traceback())
            ttl = ttl and math.min(ttl, answer.ttl) or answer.ttl
            answers[#answers+1] = ip
        end
    end

    if #answers == 0 then
        return nil, "no ip"
    end
    return answer_header, question, answers, ttl
end


-- set your preferred dns server or use default
function dns.server(host, port, ipv6)
    if not dns.launch then
        if not host then
            local f = assert(io.open "/etc/resolv.conf")
            for line in f:lines() do
                host = line:match("%s*nameserver%s+([^%s]+)")
                if host then
                    break
                end
            end
            f:close()
            assert(host, "Can't get nameserver")
        end
        dns.addr.ip   = host
        dns.addr.port = port or 53
        dns.addr.ipv6 = ipv6 or false
        dns.id = udp.open({ip="0.0.0.0", port=0, ipv6=dns.addr.ipv6})
        udp.start(dns.id)
        dns.launch = true
    end
end

function dns.close()
    udp.close(dns.id)
    dns.id = nil
    dns.addr = nil
    dns.launch = nil
end

function dns.resolve(name, ipv6, timeout)
    assert(dns.launch, "Must be executed first dns.server(host, port)")
    
    cache.refresh()
    local ip = cache.query(name, ipv6)
    if ip then
        return ip
    end
    name = name:lower()
    local b, e = name:find("https?://", 1, false)
    if b then
        name = name:sub(e+1)
    end

    local req, tid = dns.pack(name, ipv6)
    udp.sendto(dns.id, dns.addr, req)

    local msg, addr = udp.recvfrom_timeout(dns.id, timeout or dns.timeout)
    if msg then
        local header, question, answers, ttl = dns.unpack(msg)
        if header and header.tid==tid and #answers>0 then
            cache.add(question.atype, question.name, answers, ttl)
            return answers[1], answers
        end
    end
    
    return nil, nil

end

return dns

