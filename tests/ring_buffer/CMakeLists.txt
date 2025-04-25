cmake_minimum_required(VERSION 3.10)
project(ring_buffer_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GTest package
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Find Threads package
find_package(Threads REQUIRED)

# Add the test executable
add_executable(ring_buffer_test ring_buffer_test.cpp)

# Link against GTest and pthread
target_link_libraries(ring_buffer_test ${GTEST_LIBRARIES} Threads::Threads)

# Add test to CTest
add_test(NAME ring_buffer_test COMMAND ring_buffer_test)
