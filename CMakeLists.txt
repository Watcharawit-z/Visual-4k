# Top-level build.
#
# On Windows this builds the compositor. Everywhere else it builds only the
# portable self-tests, which is enough to develop and validate the resampling
# core -- the part the image quality actually depends on.

cmake_minimum_required(VERSION 3.20)
project(visual4k CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# Portable numerics self-tests, buildable on any platform.
add_executable(taptable_selftest
    tools/taptable_selftest.cpp
    host/visual4k-host/src/TapTable.cpp)
target_include_directories(taptable_selftest PRIVATE host/visual4k-host/src)

add_executable(cursor_selftest
    tools/cursor_selftest.cpp
    host/visual4k-host/src/CursorDecoder.cpp)
target_include_directories(cursor_selftest PRIVATE host/visual4k-host/src)

add_executable(edid_selftest
    tools/edid_selftest.cpp
    driver/Visual4kDisplay/Edid.cpp)
target_include_directories(edid_selftest PRIVATE driver/Visual4kDisplay)

add_test(NAME edid COMMAND edid_selftest)
add_test(NAME cursor COMMAND cursor_selftest)

# The interpreter is called python3 on Unix and python.exe on Windows, so let
# CMake find it rather than guessing. The parity check itself exits 77 when
# numpy is missing, which CTest reports as a skip.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME taptable_parity
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_taptable.py)
    set_tests_properties(taptable_parity PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        SKIP_RETURN_CODE 77)
else()
    message(STATUS "No Python interpreter found; skipping the tap table parity test")
endif()

if(WIN32)
    add_subdirectory(host/visual4k-host)
endif()
