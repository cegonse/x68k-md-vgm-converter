cmake_minimum_required(VERSION 3.22)
project(x68k_md_vgm_conv C CXX)

set(TOOL_NAME x68k-md-vgm-conv)

# zlib is a hard requirement: transparent .vgz / gzipped-.vgm decompression.
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# The tool (C99). Single-purpose; the only library it links is zlib.
# ---------------------------------------------------------------------------
file(GLOB TOOL_ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)

add_executable(${TOOL_NAME} ${TOOL_ALL_SOURCES})
target_include_directories(${TOOL_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/inc)
target_compile_options(${TOOL_NAME} PRIVATE
    -std=c99 -Wall -Wextra -Werror)
target_link_libraries(${TOOL_NAME} PRIVATE ZLIB::ZLIB)

# ---------------------------------------------------------------------------
# Tests: one executable per *.test.cpp (mirrors the cegonse/cest pattern).
# Each links the tool's C sources EXCEPT main.c (Cest provides main()).
#
# Test targets are EXCLUDE_FROM_ALL so `make` builds only the tool and needs
# no cest header; the `build_tests` aggregate builds them for `make test`.
# ---------------------------------------------------------------------------
enable_testing()

set(CEST_DIR ${CMAKE_SOURCE_DIR}/external/cest)   # populated by init.sh

# ASan/LSan on the test targets so the create/destroy pair tests catch leaks.
set(SAN_COMPILE -fsanitize=address)
set(SAN_LINK -fsanitize=address)
if(NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    list(APPEND SAN_LINK -static-libasan)
endif()

# C sources the tests link against: everything except main.c.
file(GLOB TOOL_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM TOOL_LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/main.c)

file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.test.cpp)

add_custom_target(build_tests)

foreach(TEST_FILE ${TEST_FILES})
    get_filename_component(FILE_NAME ${TEST_FILE} NAME_WE)
    set(EXEC_NAME test_${FILE_NAME})
    add_executable(${EXEC_NAME} EXCLUDE_FROM_ALL ${TEST_FILE} ${TOOL_LIB_SOURCES})
    target_include_directories(${EXEC_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/inc    # tool headers, included as <name.h>
        ${CEST_DIR})               # the 'cest' header
    # .c sources compile as C99, the .test.cpp as C++20; sanitizers on both.
    target_compile_options(${EXEC_NAME} PRIVATE
        -g -O0 -Wall -Wunused-value -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
        $<$<COMPILE_LANGUAGE:C>:-std=c99>
        ${SAN_COMPILE})
    target_link_options(${EXEC_NAME} PRIVATE ${SAN_LINK})
    target_link_libraries(${EXEC_NAME} PRIVATE ZLIB::ZLIB)
    target_compile_definitions(${EXEC_NAME} PRIVATE
        FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ${EXEC_NAME} COMMAND ${EXEC_NAME})
    add_dependencies(build_tests ${EXEC_NAME})
endforeach()
