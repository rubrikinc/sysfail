find_package(GTest REQUIRED)

# Add the test executable
add_executable(main
    lib_test.cc
    map_test.cc
)

# Include the top-level include directory for shared headers
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Link the test executable with GTest and the shared library
target_link_libraries(main PRIVATE GTest::GTest GTest::Main sysfail)

gtest_discover_tests(main)
