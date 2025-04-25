# Find GTest package
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # If GTest is not found, provide instructions
    message(STATUS "GTest not found. Tests will not be built.")
    message(STATUS "To enable tests, install GTest or configure it manually.")
    return()
endif()

# Add test executables
add_executable(connection_test
    connection/connection_test.cpp
)
target_link_libraries(connection_test
    connection
    GTest::GTest
    GTest::Main
)

add_executable(thread_pool_test
    thread_pool/thread_pool_test.cpp
)
target_link_libraries(thread_pool_test
    thread_pool
    GTest::GTest
    GTest::Main
)

add_executable(ring_buffer_test
    ring_buffer/ring_buffer_test.cpp
)
target_link_libraries(ring_buffer_test
    ring_buffer
    GTest::GTest
    GTest::Main
)

# Register tests
add_test(NAME connection_test COMMAND connection_test)
add_test(NAME thread_pool_test COMMAND thread_pool_test)
add_test(NAME ring_buffer_test COMMAND ring_buffer_test)
