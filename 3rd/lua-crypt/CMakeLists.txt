add_library(crypt SHARED lsha1.c lua-crypt.c)
set_target_properties(crypt PROPERTIES SUFFIX ".so")