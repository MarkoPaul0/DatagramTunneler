cmake_minimum_required(VERSION 3.16)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DGRAMTUNNELER_VERSION)
string(STRIP "${DGRAMTUNNELER_VERSION}" DGRAMTUNNELER_VERSION)
if(NOT DGRAMTUNNELER_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version in MAJOR.MINOR.PATCH form")
endif()

project(DatagramTunneler
    VERSION "${DGRAMTUNNELER_VERSION}"
    DESCRIPTION "Forward UDP multicast datagrams through a TCP tunnel"
    LANGUAGES CXX)

include(GNUInstallDirs)

add_executable(dgramtunneler
    src/main.cpp
    src/DatagramTunneler.cpp)

target_compile_features(dgramtunneler PRIVATE cxx_std_11)
target_compile_definitions(dgramtunneler PRIVATE
    DGRAMTUNNELER_VERSION="${PROJECT_VERSION}")
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

set(CPACK_PACKAGE_NAME "dgramtunneler")
set(CPACK_PACKAGE_VENDOR "DatagramTunneler contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Forward UDP multicast datagrams through a TCP tunnel")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
