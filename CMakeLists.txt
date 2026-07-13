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
    src/CommandLine.cpp
    src/Configuration.cpp
    src/Network.cpp
    src/Producer.cpp
    src/DatagramTunneler.cpp)

target_compile_features(dgramtunneler PRIVATE cxx_std_20)
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

if(WIN32)
    target_link_libraries(dgramtunneler PRIVATE ws2_32)
endif()

install(TARGETS dgramtunneler
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES README.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")

set(CPACK_PACKAGE_NAME "dgramtunneler")
set(CPACK_PACKAGE_VENDOR "DatagramTunneler contributors")
set(CPACK_PACKAGE_CONTACT "DatagramTunneler contributors <noreply@github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Forward UDP multicast datagrams through a TCP tunnel")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR DEB)
    set(CPACK_DEBIAN_PACKAGE_NAME "dgramtunneler")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/MarkoPaul0/DatagramTunneler")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_executable(dgramtunneler_unit_tests
        tests/unit_tests.cpp
        src/CommandLine.cpp
        src/Configuration.cpp
        src/Network.cpp
        src/DatagramTunneler.cpp)
    target_compile_features(dgramtunneler_unit_tests PRIVATE cxx_std_20)
    set_target_properties(dgramtunneler_unit_tests PROPERTIES
        CXX_EXTENSIONS OFF)
    target_include_directories(dgramtunneler_unit_tests PRIVATE src)

    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(dgramtunneler_unit_tests PRIVATE
            -Wall
            -Wconversion
            -Wsign-compare
            -Wreorder
            -Wold-style-cast
            -Werror)
    endif()

    if(WIN32)
        target_link_libraries(dgramtunneler_unit_tests PRIVATE ws2_32)
    endif()

    add_test(NAME unit_tests COMMAND dgramtunneler_unit_tests)
    add_test(NAME version_command COMMAND dgramtunneler --version)
    set_tests_properties(version_command PROPERTIES
        PASS_REGULAR_EXPRESSION "dgramtunneler [0-9]+\\.[0-9]+\\.[0-9]+")

    add_test(NAME config_path_command
        COMMAND dgramtunneler config path --config named-tunnels.toml)
    set_tests_properties(config_path_command PROPERTIES
        PASS_REGULAR_EXPRESSION "named-tunnels\\.toml")

    add_test(NAME tunnel_list_command
        COMMAND dgramtunneler tunnel list --config "${CMAKE_CURRENT_SOURCE_DIR}/tests/named_tunnels.toml")
    set_tests_properties(tunnel_list_command PROPERTIES
        PASS_REGULAR_EXPRESSION "Alias[ \\t]+Type[ \\t]+Equivalent direct command")

    add_test(NAME producer_command
        COMMAND dgramtunneler producer example-client --config "${CMAKE_CURRENT_SOURCE_DIR}/tests/named_tunnels.toml" --count 2 --interval-ms 1)
    set_tests_properties(producer_command PROPERTIES
        PASS_REGULAR_EXPRESSION "Sent Dummy datagram #2")

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tunnel_round_trip
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tunnel_round_trip.py" "$<TARGET_FILE:dgramtunneler>")
        set_tests_properties(tunnel_round_trip PROPERTIES TIMEOUT 15)
    endif()
endif()
