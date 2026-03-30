enable_testing()

find_package(GTest REQUIRED)

add_executable(test_thread_pool test_thread_pool.cpp)

target_include_directories(test_thread_pool PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_thread_pool PRIVATE
  middleware_lib
  GTest::GTest
  GTest::Main
)

include(GoogleTest)
gtest_discover_tests(test_thread_pool)
