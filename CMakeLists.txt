cmake_minimum_required(VERSION 2.8)
project(sylar)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function")

set(LIB_SRC 
    sylar/log.cc
    sylar/util.cc
    sylar/config.cc
    )

add_library(sylar SHARED ${LIB_SRC})

add_executable(test_log tests/test_log.cc)
target_link_libraries(test_log sylar)

add_executable(test_config tests/test_config.cc)
target_link_libraries(test_config sylar)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)