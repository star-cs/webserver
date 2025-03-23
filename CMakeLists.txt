cmake_minimum_required(VERSION 2.8)
project(sylar)

include (cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-builtin-macro-redefined -Wno-deprecated -Werror -Wno-unused-function")

# 头文件
include_directories(.)
include_directories(/usr/local/include) 
link_directories(/usr/local/lib)            # 一般安装后，include是头文件，lib是库文件


set(LIB_SRC 
    sylar/log.cc
    sylar/util.cc
    sylar/config.cc
    sylar/fiber.cc
    sylar/thread.cc
    sylar/mutex.cc
    sylar/scheduler.cc
    sylar/iomanager.cc
    sylar/timermanager.cc
    )

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    yaml-cpp
    pthread
)

sylar_add_executable(test_log "tests/test_log.cc" sylar "${LIBS}")
sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_util "tests/test_util.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
sylar_add_executable(test_scheduler "tests/test_scheduler.cc" sylar "${LIBS}")
sylar_add_executable(test_iomanager "tests/test_iomanager.cc" sylar "${LIBS}")
sylar_add_executable(test_timermanager "tests/test_timermanager.cc" sylar "${LIBS}")


SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)