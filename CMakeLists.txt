cmake_minimum_required(VERSION 3.22)
project(lightning_link LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Homebrew hint on macOS Apple Silicon so find_package locates SFML 3.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sfml")
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)
find_package(Threads REQUIRED)

# --- common shared library ---------------------------------------------------
add_library(ll_common STATIC
    common/serialization.cpp
    common/lagged_socket.cpp
    common/logging.cpp
    common/net_compat.cpp
)
target_include_directories(ll_common PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(ll_common PUBLIC Threads::Threads)

# --- server ------------------------------------------------------------------
add_executable(lightning_link_server
    server/main.cpp
    server/server.cpp
    server/world_state.cpp
    server/session_manager.cpp
    server/udp_snapshot_broadcaster.cpp
    server/tcp_baseline.cpp
)
target_link_libraries(lightning_link_server PRIVATE ll_common)

# --- client ------------------------------------------------------------------
add_executable(lightning_link_client
    client/main.cpp
    client/client.cpp
    client/renderer.cpp
    client/input.cpp
    client/prediction.cpp
    client/interpolation.cpp
    client/network_receiver.cpp
    client/hud.cpp
    client/bot.cpp
    client/tcp_baseline_client.cpp
)
target_link_libraries(lightning_link_client PRIVATE
    ll_common
    SFML::Graphics
    SFML::Window
    SFML::System
)

# --- tests -------------------------------------------------------------------
enable_testing()
add_executable(ll_selftest common/selftest.cpp)
target_link_libraries(ll_selftest PRIVATE ll_common)
add_test(NAME ll_selftest COMMAND ll_selftest)
