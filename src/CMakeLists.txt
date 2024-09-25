enable_language(ASM)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")

# Create the shared library
add_library(sysfail SHARED
    lib.cc
    map.cc
    restore.S
)

# Include the top-level include directory for headers
target_include_directories(sysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)
