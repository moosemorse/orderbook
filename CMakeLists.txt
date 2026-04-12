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
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# src dependencies
file(GLOB LIB_SOURCES "src/*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# main file (main.cpp)
add_executable(orderbook src/main.cpp ${LIB_SOURCES})
target_include_directories(orderbook PRIVATE include .)

enable_testing()

add_executable(
    tests/orderbook_tests
    tests/orderbook_test.cpp
)

target_link_libraries(
    orderbook_tests
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(orderbook_tests)