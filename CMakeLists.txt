cmake_minimum_required(VERSION 3.10)

# Set project name
project(SysFail)

set(CMAKE_CXX_STANDARD 23)           # Set the C++ standard to C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # Enforce the specified C++ standard
set(CMAKE_CXX_EXTENSIONS OFF)        # Disable compiler-specific extensions

# Include top-level include directory for shared headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Enable testing
enable_testing()

# Add subdirectories for source code and tests
add_subdirectory(src)
add_subdirectory(test)
