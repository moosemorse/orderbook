cmake_minimum_required(VERSION 3.14)
project(orderbook)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# enable warnings for compiler
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# keep every executable directly in build/ (Makefile, CI and README rely on it)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Threads REQUIRED)

# the order book itself, compiled once and linked into every target below
add_library(orderbook_lib STATIC src/OrderBook.cpp)
target_include_directories(orderbook_lib PUBLIC include)
target_link_libraries(orderbook_lib PUBLIC Threads::Threads)

add_subdirectory(src)
add_subdirectory(bench)

enable_testing()
add_subdirectory(tests)
