cmake_minimum_required(VERSION 3.24)

project(bored_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BORED_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(bored_configure_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    if(MSVC)
        # 源文件包含中文注释；显式使用 UTF-8，避免依赖 Windows 当前代码页。
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
        if(BORED_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(BORED_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(bored_server_core
    src/fixed_tick_clock.cpp
    src/protocol.cpp
    src/udp_socket.cpp
)

target_include_directories(bored_server_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(bored_server_core PUBLIC ws2_32)
endif()

bored_configure_target(bored_server_core)

find_package(Threads REQUIRED)

add_executable(bored_server
    src/main.cpp
)

target_link_libraries(bored_server
    PRIVATE
        bored_server_core
        Threads::Threads
)

bored_configure_target(bored_server)

include(CTest)

if(BUILD_TESTING)
    add_executable(bored_server_tests
        tests/fixed_tick_clock_test.cpp
    )

    target_link_libraries(bored_server_tests
        PRIVATE
            bored_server_core
    )

    bored_configure_target(bored_server_tests)

    add_test(NAME fixed_tick_clock_tests COMMAND bored_server_tests)

    add_executable(bored_protocol_tests
        tests/protocol_test.cpp
    )

    target_link_libraries(bored_protocol_tests
        PRIVATE
            bored_server_core
    )

    bored_configure_target(bored_protocol_tests)

    add_test(NAME protocol_tests COMMAND bored_protocol_tests)
endif()
