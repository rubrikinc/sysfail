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

find_program(VALGRIND_BIN valgrind)

if (VALGRIND_BIN)
    message(STATUS "Valgrind found: ${VALGRIND_BIN}")

    add_test(
        NAME CWrapperMemcheckedTest
        COMMAND ${VALGRIND_BIN}
            --error-exitcode=1
            --tool=memcheck
            --leak-check=full
            --errors-for-leak-kinds=definite
            --show-leak-kinds=definite $<TARGET_FILE:main>
                    --gtest_filter='CWrapper.TestMemorySafetyOfABI'
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
else()
    message(STATUS "Valgrind not found")
endif()

set(neg_test ${CMAKE_CURRENT_SOURCE_DIR}/neg_test.sh)

find_program(GO_BIN go)
if (GO_BIN)
    message(STATUS "Golang found: ${GO_BIN}")
    set(GoFFI_TestBin ${CMAKE_CURRENT_BINARY_DIR}/go_ffi)
    set(GoFFI_Src ${CMAKE_CURRENT_SOURCE_DIR}/ffi.go)

    add_custom_command(
        OUTPUT ${GoFFI_TestBin}
        COMMAND ${CMAKE_COMMAND} -E env CGO_LDFLAGS=-L${LibPath} ${GO_BIN} ARGS build -o ${GoFFI_TestBin} ${GoFFI_Src}
        DEPENDS ${GoFFI_Src}
        COMMENT "Building ffi.go"
    )

    add_custom_target(
        ffi.go ALL
        DEPENDS ${GoFFI_TestBin}
    )

    add_test(
        NAME TestGoFFI
        COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${LibPath} ${GoFFI_TestBin}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

    add_test(
        NAME TestGoFFI.NoSysfail
        COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${LibPath} EXPECT_CODE=123 NO_SYSFAIL=y  ${neg_test} ${GoFFI_TestBin}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

    add_test(
        NAME TestGoFFI.IncorrectErrorCode
        COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${LibPath} EXPECT_CODE=100 EXIT_ERRNO=1  ${neg_test} ${GoFFI_TestBin}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

else()
    message(STATUS "Golang not found")
endif()
