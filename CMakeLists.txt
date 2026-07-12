cmake_minimum_required(VERSION 3.16)

project(DatagramTunneler
    VERSION 0.1.0
    DESCRIPTION "Forward UDP multicast datagrams through a TCP tunnel"
    LANGUAGES CXX)

include(GNUInstallDirs)

add_executable(dgramtunneler
    src/main.cpp
    src/DatagramTunneler.cpp)

target_compile_features(dgramtunneler PRIVATE cxx_std_11)
set_target_properties(dgramtunneler PROPERTIES
    CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(dgramtunneler PRIVATE
        -Wall
        -Wconversion
        -Wsign-compare
        -Wreorder
        -Wold-style-cast
        -Werror)
endif()

install(TARGETS dgramtunneler
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
