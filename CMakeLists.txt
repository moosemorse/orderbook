cmake_minimum_required(VERSION 3.14)
project(orderbook)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# enable warnings for compiler
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# google test dependency
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(Threads REQUIRED)

# the order book itself, compiled once and linked into every target below
add_library(orderbook_lib STATIC src/OrderBook.cpp)
target_include_directories(orderbook_lib PUBLIC include)
target_link_libraries(orderbook_lib PUBLIC Threads::Threads)

# main file (main.cpp)
add_executable(orderbook src/main.cpp)
target_link_libraries(orderbook PRIVATE orderbook_lib)

# benchmark harness - timing numbers are local-only, but a small run's
# fingerprint is checked in ctest (see bench_fingerprint below)
add_executable(orderbook_bench bench/main.cpp)
target_include_directories(orderbook_bench PRIVATE bench)
target_link_libraries(orderbook_bench PRIVATE orderbook_lib)

enable_testing()

add_executable(
    orderbook_tests
    tests/orderbook_tests.cpp
)

# pch.h lives in the repo root
target_include_directories(orderbook_tests PRIVATE .)

target_link_libraries(
    orderbook_tests
    orderbook_lib
    GTest::gtest_main
)

# test data files, tests resolve them relative to their working directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestFolder DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

include(GoogleTest)
gtest_discover_tests(orderbook_tests)

# regression check: a fixed-seed run must reproduce this fingerprint. If a
# change is *meant* to alter matching behaviour, re-run the command and update it.
# (generator uses <random> distributions, so the value is libstdc++-specific)
add_test(
    NAME bench_fingerprint
    COMMAND orderbook_bench --orders 100000 --warmup 10000 --core 0 --check-every 1000 --check a6050a70fe758d75
)
