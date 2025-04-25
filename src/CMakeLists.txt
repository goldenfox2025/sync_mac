# Add libraries
add_library(connection
    connection/connection.cpp
)

add_library(thread_pool
    thread_pool/thread_pool.cpp
)

# Ring buffer is header-only, but we need to create a library target for it
add_library(ring_buffer INTERFACE)
target_include_directories(ring_buffer INTERFACE ${CMAKE_SOURCE_DIR}/include)

add_library(common
    common/common.cpp
)

# Create empty main.cpp file if it doesn't exist
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp "")
