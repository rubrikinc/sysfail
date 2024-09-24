# Create the shared library
add_library(sysfail SHARED
    lib.cc
    map.cc
)

# Include the top-level include directory for headers
target_include_directories(sysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)
