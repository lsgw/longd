cmake_minimum_required( VERSION 3.6.3 )
project(longnet)

#SET (CMAKE_VERBOSE_MAKEFILE ON)
#SET (CMAKE_BUILD_TYPE "Debug")
SET (CMAKE_BUILD_TYPE "Release")

IF (APPLE)
	SET (CMAKE_MACOSX_RPATH 0)
	SET (CMAKE_SHARED_LINKER_FLAGS  "-dynamiclib -Wl,-undefined,dynamic_lookup")
ENDIF(APPLE)


SET (CMAKE_C_COMPILER        "gcc")
SET (CMAKE_C_FLAGS           "-std=c99")
SET (CMAKE_C_FLAGS_DEBUG     "-O0 -g -Wall")
SET (CMAKE_C_FLAGS_RELEASE   "-O2 -g -Wall")

SET (CMAKE_CXX_COMPILER      "g++")
SET (CMAKE_CXX_FLAGS         "-std=c++1y")
SET (CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -Wall")
SET (CMAKE_CXX_FLAGS_RELEASE "-O2 -g -Wall")

SET (EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin/)
SET (LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/lib/)

include_directories(
	${CMAKE_SOURCE_DIR}/src/core/base/
    ${CMAKE_SOURCE_DIR}/src/core/context/
    ${CMAKE_SOURCE_DIR}/src/core/port/
    
    ${CMAKE_SOURCE_DIR}/src/core/
    ${CMAKE_SOURCE_DIR}/src/driver/
    ${CMAKE_SOURCE_DIR}/src/driver/tcp/
    ${CMAKE_SOURCE_DIR}/src/driver/udp/
    ${CMAKE_SOURCE_DIR}/src/driver/sig/
    ${CMAKE_SOURCE_DIR}/src/driver/exe/
    ${CMAKE_SOURCE_DIR}/src/service/

    ${CMAKE_SOURCE_DIR}/3rd/
    ${CMAKE_SOURCE_DIR}/3rd/lua
    ${CMAKE_SOURCE_DIR}/3rd/rapidjson
)

add_subdirectory(3rd)
add_subdirectory(src)
add_subdirectory(example/simple/exe)
