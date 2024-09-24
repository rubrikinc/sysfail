# Create the shared library
add_library(libsysfail SHARED
    lib.cc
)

# Create the executable (binary)
add_executable(sysfail
    main.cc
)

# Include the top-level include directory for headers
target_include_directories(libsysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(sysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Link the shared library to the tool
target_link_libraries(sysfail PRIVATE libsysfail)
