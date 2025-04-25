cmake_minimum_required(VERSION 3.10)
project(sync_mac VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Main executable
add_executable(sync_mac src/main.cpp)
target_link_libraries(sync_mac
    connection
    thread_pool
    ring_buffer
    common
)

# Enable testing
enable_testing()
