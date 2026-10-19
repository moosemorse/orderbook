# timing numbers are local-only; tests/regression_tests.cpp reuses the generator
add_executable(orderbook_bench main.cpp)
target_link_libraries(orderbook_bench PRIVATE orderbook_lib)
