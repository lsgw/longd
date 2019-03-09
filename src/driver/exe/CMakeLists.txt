add_library(exe SHARED exe.cc)
set_target_properties(exe PROPERTIES SUFFIX ".so")