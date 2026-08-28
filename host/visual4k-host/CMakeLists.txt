# visual4k-host -- the user-mode compositor.
#
# Windows only; it links DXGI, D3D11 and the runtime shader compiler.
# The tap-table code is deliberately platform-neutral and is also built on
# other platforms by tools/, so that the numerics stay testable off Windows.

cmake_minimum_required(VERSION 3.20)
project(visual4k_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR
        "visual4k-host targets Windows. To work on the resampling core "
        "elsewhere, build tools/taptable_selftest.cpp instead.")
endif()

add_executable(visual4k-host
    src/main.cpp
    src/Renderer.cpp
    src/Duplicator.cpp
    src/TapTable.cpp
    src/CursorDecoder.cpp
)

target_compile_definitions(visual4k-host PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(visual4k-host PRIVATE
    d3d11 dxgi d3dcompiler)

if(MSVC)
    target_compile_options(visual4k-host PRIVATE /W4 /permissive-)
endif()

# The shaders are compiled at runtime so the filter can be tuned without a
# rebuild; they have to sit next to the executable for that to work.
add_custom_command(TARGET visual4k-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../../shaders
            $<TARGET_FILE_DIR:visual4k-host>/shaders
    COMMENT "Staging shaders next to visual4k-host")
