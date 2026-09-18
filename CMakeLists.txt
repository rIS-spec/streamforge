cmake_minimum_required(VERSION 3.20)
project(StreamForge CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(streamforge src/main.cpp)
target_include_directories(streamforge PRIVATE include)

# For testing with googletest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_executable(test_message tests/test_message.cpp)
target_include_directories(test_message PRIVATE include)
target_link_libraries(test_message PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(test_message)

add_executable(test_topic tests/test_topic.cpp)
target_include_directories(test_topic PRIVATE include)
target_link_libraries(test_topic PRIVATE GTest::gtest_main)
gtest_discover_tests(test_topic)

add_executable(test_topic_concurrency tests/test_topic_concurrency.cpp)
target_include_directories(test_topic_concurrency PRIVATE include)
target_link_libraries(test_topic_concurrency PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_topic_concurrency)