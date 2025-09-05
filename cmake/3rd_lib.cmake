include(ExternalProject)
include_directories(${PROJECT_BINARY_DIR}/include)
link_directories(${PROJECT_BINARY_DIR}/lib)
link_directories(${PROJECT_BINARY_DIR}/lib64)

ExternalProject_Add(libspdlog
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY https://gitee.com/jbl19860422/spdlog.git
    GIT_TAG v1.14.0
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}
    BUILD_COMMAND make -j4 -C build 
    INSTALL_COMMAND make -C build install
)

ExternalProject_Add(libyamlcpp
    EXCLUDE_FROM_ALL 1
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.tar.gz
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND cmake -B build -DYAML_CPP_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}
    BUILD_COMMAND make -C build  -j4
    INSTALL_COMMAND make -C build install -j4
)

ExternalProject_Add(libjsoncpp
    EXCLUDE_FROM_ALL 1
    URL https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.8.4.tar.gz
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}
    BUILD_COMMAND make -C build -j4
    INSTALL_COMMAND make -C build install -j4
)

ExternalProject_Add(libgtest
    EXCLUDE_FROM_ALL 1
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}
    BUILD_COMMAND make -C build -j4
    INSTALL_COMMAND make -C build install -j4 
)

find_package(OpenSSL REQUIRED)
message(${OPENSSL_CRYPTO_LIBRARY})
message(${OPENSSL_SSL_LIBRARY})

include_directories(${OPENSSL_INCLUDE_DIR})
