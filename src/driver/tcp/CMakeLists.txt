add_library(TcpListener SHARED TcpListener.cc)
set_target_properties(TcpListener PROPERTIES SUFFIX ".so")

add_library(TcpConnection SHARED TcpConnection.cc)
set_target_properties(TcpConnection PROPERTIES SUFFIX ".so")

add_library(TcpClient SHARED TcpClient.cc)
set_target_properties(TcpClient PROPERTIES SUFFIX ".so")