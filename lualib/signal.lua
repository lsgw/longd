local system = require "system"
local api    = require "api"
local codec  = require "api.sig.codec"

local signal = { handler = { }, id = api.sig(), map = { },
type  = {
    kActionModule = 1,
    kActionDriver = 2,
    kCancelModule = 3,
    kCancelDriver = 4,
    kHappen       = 5,
    kActionCHLD   = 6,
    kCancelCHLD   = 7,
    kHappenCHLD   = 8,
    kInfo         = 9,
},
linux = {
    SIGHUP    =  1, -- 终止进程       终端线路挂断
    SIGINT    =  2, -- 终止进程       来自键盘的中断 ctrl-z
    SIGQUIT   =  3, -- 终止进程       来自键盘的退出 ctrl-c
    SIGILL    =  4, -- 终止进程       非法指令
    SIGTRAP   =  5, -- 建立CORE文件   跟踪自陷
    SIGABRT   =  6, -- 建立CORE文件   来自 abort 函数的终止信号
    SIGIOT    =  6,
    SIGBUS    =  7, -- 终止进程       总线错误
    SIGFPE    =  8, -- 建立CORE文件   浮点异常
    SIGKILL   =  9, -- 终止进程       杀死进程
    SIGUSR1   = 10, -- 终止进程       用户定义信号1
    SIGSEGV   = 11, -- 建立CORE文件   段非法错误
    SIGUSR2   = 12, -- 终止进程       用户定义信号2
    SIGPIPE   = 13, -- 终止进程       向一个没有读用户的管道写数据
    SIGALRM   = 14, -- 终止进程       来自 alarm 函数的定时器信号
    SIGTERM   = 15, -- 终止进程       软件终止信号
    SIGSTKFLT = 16, -- 终止进程       协处理器上的栈故障
    SIGCHLD   = 17, -- 忽略信号       当子进程停止或退出时通知父进程的信号
    SIGCLD    = 17,
    SIGCONT   = 18, -- 忽略信号       继续执行一个停止的进程
    SIGSTOP   = 19, -- 停止进程       非终端来的停止信号
    SIGTSTP   = 20, -- 停止进程       终端来的停止信号
    SIGTTIN   = 21, -- 停止进程       后台进程从终端读
    SIGTTOU   = 22, -- 停止进程       后台进程向终端写
    SIGURG    = 23, -- 忽略信号       套接字上的紧急情况信号
    SIGXCPU   = 24, -- 终止进程       CPU时限超时
    SIGXFSZ   = 25, -- 终止进程       文件长度过长
    SIGVTALRM = 26, -- 终止进程       虚拟计时器到时
    SIGPROF   = 27, -- 终止进程       统计分布图用计时器到时
    SIGWINCH  = 28, -- 忽略信号       窗口大小发生变化
    SIGIO     = 29, -- 终止进程       描述符上可以进行I/O操作
    SIGPOLL   = 29,
    SIGPWR    = 30, -- 终止进程       电源故障
    SIGSYS    = 31, -- 终止进程       无效的系统调用
    SIGUNUSED = 31,
},
darwin = {
    SIGHUP    =  1,
    SIGINT    =  2, 
    SIGQUIT   =  3, 
    SIGILL    =  4,
    SIGTRAP   =  5,
    SIGABRT   =  6,
    SIGEMT    =  7, 
    SIGFPE    =  8,
    SIGKILL   =  9,
    SIGBUS    = 10,
    SIGSEGV   = 11,
    SIGSYS    = 12,
    SIGPIPE   = 13,
    SIGALRM   = 14,
    SIGTERM   = 15, 
    SIGURG    = 16,
    SIGSTOP   = 17, 
    SIGTSTP   = 18, 
    SIGCONT   = 19, 
    SIGCHLD   = 20,
    SIGTTIN   = 21, 
    SIGTTOU   = 22, 
    SIGIO     = 23,
    SIGXCPU   = 24,
    SIGXFSZ   = 25,
    SIGVTALRM = 26,  
    SIGPROF   = 27,
    SIGWINCH  = 28,
    SIGINFO   = 29,
    SIGUSR1   = 30,
    SIGUSR2   = 31,
}}
do
    system.protocol.register({
        name     = "signal",
        id       = system.protocol.MSG_TYPE_SIG,
        pack     = codec.encode_to_lightuserdata,
        unpack   = codec.decode_from_lightuserdata,
        dispatch = function(type, subtype, sig)
            assert(subtype == signal.type.kHappen)
            if signal.handler[sig] then
                signal.handler[sig](sig)
            else
                assert(false, "signal cannot install dispatch function " .. string.format("handle[%08x] sig[%d]", system.self(), sig))
            end
        end
    })

    local info = io.popen('uname -s')
    local platform = info:read("*all")
    info:close()

    if platform:match("Darwin") then
        signal.map = signal.darwin
    elseif platform:match("Linux") then
        signal.map = signal.linux
    else
        assert(false, "platform info error")
    end
end

function signal.action(sig, func)
    assert(type(func) == "function")
    if type(sig) == "string" and signal.map[sig]  then
        local udata, nbyte = codec.encode_to_lightuserdata("action", system.self(), signal.map[sig])
        local succ, err = api.command(signal.id, system.protocol.MSG_TYPE_SIG, udata, nbyte)
        assert(succ, tostring(err))
        signal.handler[signal.map[sig]] = func
    elseif type(sig) == "number" and sig > 0 and sig < 32 then
        local udata, nbyte = codec.encode_to_lightuserdata("action", system.self(), sig)
        local succ, err = api.command(signal.id, system.protocol.MSG_TYPE_SIG, udata, nbyte)
        assert(succ, tostring(err))
        signal.handler[sig] = func
    else
        assert(false, "install signal handler error")
    end
end

function signal.cancel(sig)
    if type(sig) == "string" and signal.map[sig]  then
        local udata, nbyte = codec.encode_to_lightuserdata("cancel" ,system.self(), signal.map[sig])
        local succ, err = api.command(signal.id, system.protocol.MSG_TYPE_SIG, udata, nbyte)
        assert(succ, tostring(err))
        signal.handler[signal.map[sig]] = nil
    elseif type(sig) == "number" and sig > 0 and sig < 32 then
        local udata, nbyte = codec.encode_to_lightuserdata("cancel", system.self(), sig)
        local succ, err = api.command(signal.id, system.protocol.MSG_TYPE_SIG, udata, nbyte)
        assert(succ, tostring(err))
        signal.handler[sig] = nil
    else
        assert(false, "cancel signal handler error")
    end
end

function signal.info()
    local udata, nbyte = codec.encode_to_lightuserdata("info")
    local succ, err = api.command(signal.id, system.protocol.MSG_TYPE_SIG, udata, nbyte)
    assert(succ, tostring(err))
    return system.receive {
        [{"signal", signal.type.kInfo}] = function(type, subtype, ...)
            return ...
        end
    }
end


return signal