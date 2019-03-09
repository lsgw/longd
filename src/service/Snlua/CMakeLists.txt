add_library(Snlua SHARED Snlua.cpp)
set_target_properties(Snlua PROPERTIES SUFFIX ".so")


set(src
	api.cpp
	api_log.cpp
	api_udp.cpp
	api_tcp.cpp
	api_sig.cpp
	api_exe.cpp
)
add_library(api SHARED ${src})
set_target_properties(api PROPERTIES SUFFIX ".so")