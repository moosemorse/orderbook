# google test dependency, only fetched when the tests are configured
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(
    orderbook_tests
    orderbook_tests.cpp
    regression_tests.cpp
)

# pch.hpp lives in the repo root; the regression test uses the bench's generator
target_include_directories(orderbook_tests PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/bench)

target_link_libraries(
    orderbook_tests
    orderbook_lib
    GTest::gtest_main
)

# test data files, tests resolve them relative to their working directory
file(COPY TestFolder DESTINATION ${CMAKE_BINARY_DIR})

include(GoogleTest)
gtest_discover_tests(orderbook_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
