cmake_minimum_required(VERSION 3.5)
project(sylar)

include (cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE OFF)
# 全局编译选项配置
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 公共编译选项
add_compile_options(
    -rdynamic
    -O0                     # O0 debug（开发），O2 release（编译会慢很多）
    -ggdb
    -Wall
    -Wno-builtin-macro-redefined
    -Wno-deprecated
    -Werror
    -Wno-unused-function
)
enable_language(ASM)

# 头文件
include_directories(.)
include_directories(/usr/local/include) 
link_directories(/usr/local/lib)            # 一般安装后，include是头文件，lib是库文件

set(LIB_SRC
    sylar/config.cc
    sylar/bytearray.cc
    sylar/config_init.cc
    sylar/fd_manager.cc 
    sylar/mutex.cc
    sylar/memorypool.cc
    sylar/fiber.cc
    sylar/hook.cc
    sylar/iomanager.cc
    sylar/buffermanager.cc
    sylar/log.cc
    sylar/scheduler.cc
    sylar/timermanager.cc
    sylar/thread.cc
    sylar/util.cc
    sylar/worker.cc
    sylar/address.cc
    sylar/socket.cc
    fcontext_asm/jump_x86_64_sysv_elf_gas.S
    fcontext_asm/make_x86_64_sysv_elf_gas.S
    )

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    yaml-cpp
    pthread
    dl
)

sylar_add_executable(test_log "tests/test_log.cc" sylar "${LIBS}")
sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_util "tests/test_util.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
sylar_add_executable(test_scheduler "tests/test_scheduler.cc" sylar "${LIBS}")
sylar_add_executable(test_iomanager "tests/test_iomanager.cc" sylar "${LIBS}")
sylar_add_executable(test_timer "tests/test_timermanager.cc" sylar "${LIBS}")
sylar_add_executable(test_hook "tests/test_hook.cc" sylar "${LIBS}")
sylar_add_executable(test_memorypool "tests/test_memorypool.cc" sylar "${LIBS}")
sylar_add_executable(test_address "tests/test_address.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_server "tests/test_socket_tcp_server.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_client "tests/test_socket_tcp_client.cc" sylar "${LIBS}")
sylar_add_executable(test_socket "tests/test_socket.cc" sylar "${LIBS}")
sylar_add_executable(test_bytearray "tests/test_bytearray.cc" sylar "${LIBS}")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)