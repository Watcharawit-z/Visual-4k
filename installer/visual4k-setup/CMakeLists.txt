# Visual4k-Setup -- the double-click installer.
#
# Windows only, and deliberately dependency-free: it links nothing but the
# system libraries, so the shipped exe runs on a machine with no runtime, no
# WDK, and no PowerShell execution policy exemption.

cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
    message(FATAL_ERROR "visual4k-setup targets Windows")
endif()

add_executable(visual4k-setup
    src/main.cpp
    src/Ui.cpp
    src/CertTrust.cpp
    src/TestSigning.cpp
    src/DeviceInstall.cpp
    src/DisplayConfig.cpp
)

set_target_properties(visual4k-setup PROPERTIES OUTPUT_NAME "Visual4k-Setup")

target_compile_definitions(visual4k-setup PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# setupapi and newdev are what devcon uses to create a root-enumerated device;
# crypt32 puts the build's certificate into the machine trust stores.
target_link_libraries(visual4k-setup PRIVATE
    setupapi newdev crypt32 advapi32 user32)

if(MSVC)
    target_compile_options(visual4k-setup PRIVATE /W4 /permissive-)
    # Embedding the manifest is what makes a double-click raise the UAC prompt.
    # Without it the program starts unelevated and fails at its first step.
    target_link_options(visual4k-setup PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/visual4k-setup.manifest")
endif()
