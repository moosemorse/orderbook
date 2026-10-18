# benchmark harness - timing numbers are local-only, but a small run's
# fingerprint is checked in ctest (see bench_fingerprint in tests/CMakeLists.txt)
add_executable(orderbook_bench main.cpp)
target_link_libraries(orderbook_bench PRIVATE orderbook_lib)
