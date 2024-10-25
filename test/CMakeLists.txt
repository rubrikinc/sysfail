find_package(GTest REQUIRED)

# Include the top-level include directory for headers
target_include_directories(sysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(cisq SHARED
    cisq.cc
)

# Add the test executable
add_executable(main
    session_test.cc
    map_test.cc
    thdmon_test.cc
    session_thdmon_test.cc
    cwrapper_test.cc
)

# Include the top-level include directory for shared headers
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/test)

target_include_directories(cisq PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(cisq PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Link the test executable with GTest and the shared library
target_link_libraries(main PRIVATE GTest::GTest GTest::Main sysfail cisq)

gtest_discover_tests(main)
