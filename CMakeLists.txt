cmake_minimum_required(VERSION 3.10)
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
    -O0                     # O0 debug（开发），O2 release（编译会慢很多）
    -ggdb
    -Wall
    -Wno-builtin-macro-redefined
    -Wno-deprecated
    -Wno-error=deprecated-declarations
    -Werror
    -Wno-unused-function
)
enable_language(ASM)

# 头文件
include_directories(.)
include_directories(/usr/local/include) 
include_directories(/usr/include/postgresql)
link_directories(/usr/local/lib)            # 一般安装后，include是头文件，lib是库文件

include_directories(/usr/include/jsoncpp)
# find_package(hiredis REQUIRED)
# find_package(MySQLConnector REQUIRED) # 查找 MySQL Connector/C++
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()
find_package(Protobuf)
if(Protobuf_FOUND)
    include_directories(${Protobuf_INCLUDE_DIRS})
endif()

file(GLOB_RECURSE CORE_FILES sylar/core/*.cc sylar/core/fcontext_asm/*.S sylar/core/dag/*.cc)
file(GLOB_RECURSE IO_FILES sylar/io/db/*.cc sylar/io/util/*.cc)
file(GLOB_RECURSE NET_FILES sylar/net/*.cc sylar/net/http/http-parser/http_parser.c)

add_library(sylar SHARED ${CORE_FILES} ${IO_FILES} ${NET_FILES})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    dl
    tbb
    pthread
    yaml-cpp
    jsoncpp
    ${OPENSSL_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    event
    hiredis_vip
    mysqlclient
    sqlite3
    pq
    tinyxml2
    gtest
    gtest_main
    jemalloc
)
# sylar_add_executable(test_log "tests/core/test_log.cc" sylar "${LIBS}")
# sylar_add_executable(test_config "tests/core/test_config.cc" sylar "${LIBS}")
# sylar_add_executable(test_thread "tests/core/test_thread.cc" sylar "${LIBS}")
# sylar_add_executable(test_util "tests/core/test_util.cc" sylar "${LIBS}")
# sylar_add_executable(test_fiber "tests/core/test_fiber.cc" sylar "${LIBS}")
# sylar_add_executable(test_scheduler "tests/core/test_scheduler.cc" sylar "${LIBS}")
# sylar_add_executable(test_iomanager "tests/core/test_iomanager.cc" sylar "${LIBS}")
# sylar_add_executable(test_timer "tests/core/test_timermanager.cc" sylar "${LIBS}")
# sylar_add_executable(test_hook "tests/core/test_hook.cc" sylar "${LIBS}")
# sylar_add_executable(test_memorypool "tests/core/test_memorypool.cc" sylar "${LIBS}")
# sylar_add_executable(test_env "tests/core/test_env.cc" sylar "${LIBS}")
# sylar_add_executable(test_fiber_stack_overflow "tests/core/test_fiber_stack_overflow.cc" sylar "${LIBS}")
# sylar_add_executable(test_daemon "tests/core/test_daemon.cc" sylar "${LIBS}")

# sylar_add_executable(test_address "tests/net/test_address.cc" sylar "${LIBS}")
# sylar_add_executable(test_socket_tcp_server "tests/net/test_socket_tcp_server.cc" sylar "${LIBS}")
# sylar_add_executable(test_socket_tcp_client "tests/net/test_socket_tcp_client.cc" sylar "${LIBS}")
# sylar_add_executable(test_socket "tests/net/test_socket.cc" sylar "${LIBS}")
# sylar_add_executable(test_bytearray "tests/net/test_bytearray.cc" sylar "${LIBS}")
# sylar_add_executable(test_tcp_server "tests/net/test_tcp_server.cc" sylar "${LIBS}")
# sylar_add_executable(test_http_parser "tests/net/http/test_http_parser.cc" sylar "${LIBS}")
# sylar_add_executable(test_http_server "tests/net/http/test_http_server.cc" sylar "${LIBS}")
# sylar_add_executable(test_http_connection "tests/net/http/test_http_connection.cc" sylar "${LIBS}")
# sylar_add_executable(test_http2_client "tests/net/http2/http2_client.cc" sylar "${LIBS}")
# sylar_add_executable(test_http2_server "tests/net/http2/http2_server.cc" sylar "${LIBS}")

# sylar_add_executable(test_mysql "tests/io/test_mysql.cc" sylar "${LIBS}")
# sylar_add_executable(test_postgresql "tests/io/test_postgresql.cc" sylar "${LIBS}")

# sylar_add_executable(test_rock "tests/net/rock/test_rock.cc" sylar "${LIBS}")

# sylar_add_executable(test_ws_server "tests/net/ws/test_ws_server.cc" sylar "${LIBS}")
# sylar_add_executable(test_ws_client "tests/net/ws/test_ws_client.cc" sylar "${LIBS}")
# sylar_add_executable(test_dag "tests/core/test_dag.cc" sylar "${LIBS}")

set(ORM_SRCS
    sylar/io/orm/orm.cc
    sylar/io/orm/column.cc
    sylar/io/orm/index.cc
    sylar/io/orm/table.cc
    sylar/io/orm/util.cc
    )
sylar_add_executable(orm "${ORM_SRCS}" sylar "${LIBS}")

sylar_add_executable(bin_sylar "sylar/main.cc" sylar "${LIBS}")
set_target_properties(bin_sylar PROPERTIES OUTPUT_NAME "sylar")

# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/orm_out)
# set(OLIBS ${LIBS} orm_data)
# sylar_add_executable(test_orm "tests/io/test_orm.cc" orm_data "${OLIBS}")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)