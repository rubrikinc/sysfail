cmake_minimum_required(VERSION 3.10)

# Set project name
project(SysFail)

# Include top-level include directory for shared headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Enable testing
enable_testing()

# Add subdirectories for source code and tests
add_subdirectory(src)
add_subdirectory(test)
