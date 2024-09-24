find_package(GTest REQUIRED)

# Add the test executable
add_executable(main_test
    main_test.cc
)

# Include the top-level include directory for shared headers
target_include_directories(main_test PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Link the test executable with GTest and the shared library
target_link_libraries(main_test PRIVATE GTest::GTest GTest::Main sysfail)

gtest_discover_tests(main_test)
