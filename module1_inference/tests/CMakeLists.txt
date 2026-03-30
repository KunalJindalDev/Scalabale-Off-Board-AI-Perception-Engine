enable_testing()

find_package(GTest REQUIRED)

add_executable(test_model_runner test_model_runner.cpp)

target_include_directories(test_model_runner PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_model_runner PRIVATE
  inference_lib     # the static library defined in the parent CMakeLists.txt
  GTest::GTest
  GTest::Main
)

include(GoogleTest)
gtest_discover_tests(test_model_runner)
