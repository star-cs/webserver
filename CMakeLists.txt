cmake_minimum_required(VERSION 3.5)
project(sylar)

include (cmake/utils.cmake)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 
set(CMAKE_VERBOSE_MAKEFILE OFF)
# 全局编译选项配置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 公共编译选项
add_compile_options(
    -rdynamic
    -O3                     # O0 debug（开发），O2 release（编译会慢很多）
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

# find_package(MySQLConnector REQUIRED) # 查找 MySQL Connector/C++

set(LIB_SRC
    sylar/core/config/config.cc
    sylar/core/config/config_init.cc
    sylar/core/fd_manager.cc 
    sylar/core/mutex.cc
    sylar/core/memory/memorypool.cc
    sylar/core/fiber.cc
    sylar/core/hook.cc
    sylar/core/iomanager.cc
    sylar/core/common/buffermanager.cc
    sylar/core/signal/signal.cc
    sylar/core/log/log.cc
    sylar/core/scheduler.cc
    sylar/core/timermanager.cc
    sylar/core/thread.cc
    sylar/core/util/util.cc
    sylar/core/worker.cc
    sylar/core/env.cc
    sylar/core/daemon.cc

    sylar/net/bytearray.cc
    sylar/net/address.cc
    sylar/net/socket.cc
    sylar/net/tcp_server.cc
    sylar/net/http/http-parser/http_parser.c
    sylar/net/http/http.cc
    sylar/net/http/http_parser.cc

    sylar/net/stream.cc
    sylar/net/streams/socket_stream.cc
    sylar/net/http/http_session.cc
    sylar/net/http/servlet.cc
    sylar/net/http/http_server.cc
    sylar/net/uri.cc
    sylar/net/http/http_connection.cc
    
    sylar/core/fcontext_asm/jump_x86_64_sysv_elf_gas.S
    sylar/core/fcontext_asm/make_x86_64_sysv_elf_gas.S

    sylar/io/db/mysql.cc
    )

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    yaml-cpp
    pthread
    dl
    mysqlclient  
)

sylar_add_executable(test_log "tests/core/test_log.cc" sylar "${LIBS}")
sylar_add_executable(test_config "tests/core/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/core/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_util "tests/core/test_util.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber "tests/core/test_fiber.cc" sylar "${LIBS}")
sylar_add_executable(test_scheduler "tests/core/test_scheduler.cc" sylar "${LIBS}")
sylar_add_executable(test_iomanager "tests/core/test_iomanager.cc" sylar "${LIBS}")
sylar_add_executable(test_timer "tests/core/test_timermanager.cc" sylar "${LIBS}")
sylar_add_executable(test_hook "tests/core/test_hook.cc" sylar "${LIBS}")
sylar_add_executable(test_memorypool "tests/core/test_memorypool.cc" sylar "${LIBS}")
sylar_add_executable(test_env "tests/core/test_env.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber_stack_overflow "tests/core/test_fiber_stack_overflow.cc" sylar "${LIBS}")
sylar_add_executable(test_daemon "tests/core/test_daemon.cc" sylar "${LIBS}")

sylar_add_executable(test_address "tests/net/test_address.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_server "tests/net/test_socket_tcp_server.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_client "tests/net/test_socket_tcp_client.cc" sylar "${LIBS}")
sylar_add_executable(test_socket "tests/net/test_socket.cc" sylar "${LIBS}")
sylar_add_executable(test_bytearray "tests/net/test_bytearray.cc" sylar "${LIBS}")
sylar_add_executable(test_tcp_server "tests/net/test_tcp_server.cc" sylar "${LIBS}")
sylar_add_executable(test_http_parser "tests/net/http/test_http_parser.cc" sylar "${LIBS}")
sylar_add_executable(test_http_server "tests/net/http/test_http_server.cc" sylar "${LIBS}")
sylar_add_executable(test_http_connection "tests/net/http/test_http_connection.cc" sylar "${LIBS}")


SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)