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

add_executable(test_thread_pool tests/test_thread_pool.cpp)
target_include_directories(test_thread_pool PRIVATE include)
target_link_libraries(test_thread_pool PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_thread_pool)

add_executable(server_experiment src/server_experiment.cpp)

add_executable(broker_server src/broker_server.cpp)
target_include_directories(broker_server PRIVATE include)
target_link_libraries(broker_server PRIVATE Threads::Threads)

add_executable(test_protocol tests/test_protocol.cpp)
target_include_directories(test_protocol PRIVATE include)
target_link_libraries(test_protocol PRIVATE GTest::gtest_main)
gtest_discover_tests(test_protocol)

add_executable(test_client src/test_client.cpp)
target_include_directories(test_client PRIVATE include)

add_executable(test_log tests/test_log.cpp)
target_include_directories(test_log PRIVATE include)
target_link_libraries(test_log PRIVATE GTest::gtest_main)
gtest_discover_tests(test_log)

add_executable(test_broker tests/test_broker.cpp)
target_include_directories(test_broker PRIVATE include)
target_link_libraries(test_broker PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_broker)

add_executable(epoll_experiment src/epoll_experiment.cpp)

add_executable(broker_server_epoll src/broker_server_epoll.cpp)
target_include_directories(broker_server_epoll PRIVATE include)
target_link_libraries(broker_server_epoll PRIVATE Threads::Threads)

add_executable(test_recovery tests/test_recovery.cpp)
target_include_directories(test_recovery PRIVATE include)
target_link_libraries(test_recovery PRIVATE GTest::gtest_main)
gtest_discover_tests(test_recovery)

add_executable(benchmark_client src/benchmark_client.cpp)
target_include_directories(benchmark_client PRIVATE include)
target_link_libraries(benchmark_client PRIVATE Threads::Threads)

add_executable(sf_client src/sf_client.cpp)
target_include_directories(sf_client PRIVATE include)