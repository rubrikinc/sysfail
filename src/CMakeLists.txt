enable_language(ASM)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")

find_package(TBB REQUIRED)
# Create the shared library
add_library(sysfail STATIC
    session.cc
    map.cc
    log.cc
    syscall.cc
    thdmon.cc
    signal.cc
    restore.S
    cwrapper.cc
    inv_pred.cc
)

target_link_libraries(sysfail TBB::tbb)

set(inc_dir ${CMAKE_SOURCE_DIR}/include)

# Include the top-level include directory for headers
target_include_directories(sysfail PUBLIC ${inc_dir})

install(TARGETS sysfail
        LIBRARY DESTINATION lib)

install(DIRECTORY ${inc_dir}/
        DESTINATION include)
