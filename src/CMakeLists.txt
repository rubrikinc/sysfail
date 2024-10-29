enable_language(ASM)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp")

find_package(TBB REQUIRED)
# Create the shared library
add_library(sysfail SHARED
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

# Include the top-level include directory for headers
target_include_directories(sysfail PUBLIC ${CMAKE_SOURCE_DIR}/include)
