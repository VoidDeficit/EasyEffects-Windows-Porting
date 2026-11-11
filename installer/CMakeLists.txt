# EEVirtualAudioSetup.exe - self-contained installer for the EasyEffects Virtual Audio driver.
#
# Build the driver first (driver/EEVirtualAudio.vcxproj), then:
#   cmake -S installer -B build-installer -A x64
#   cmake --build build-installer --config Release
#
# Or use scripts/build_driver_installer.ps1, which does both.
#
# For a Secure-Boot-compatible release, point EEVAD_PACKAGE_DIR at the
# Microsoft attestation-signed package and leave EEVAD_TEST_CERT empty:
#   -DEEVAD_PACKAGE_DIR=<signed package folder> -DEEVAD_TEST_CERT=

cmake_minimum_required(VERSION 3.20)
project(EEVirtualAudioSetup LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(_driver_out "${CMAKE_CURRENT_SOURCE_DIR}/../driver/build/x64/Release")
set(_default_cert "")
if(EXISTS "${_driver_out}/EEVirtualAudio.cer")
    set(_default_cert "${_driver_out}/EEVirtualAudio.cer")
endif()

set(EEVAD_PACKAGE_DIR "${_driver_out}/EEVirtualAudio" CACHE PATH
    "Folder with EEVirtualAudio.sys/.inf/.cat to embed")
set(EEVAD_TEST_CERT "${_default_cert}" CACHE FILEPATH
    "Test certificate to embed (empty for a Microsoft-signed release package)")

# Locate package files (case-insensitive: the WDK writes the .cat in lowercase).
function(eevad_find_file out name)
    file(GLOB _candidates "${EEVAD_PACKAGE_DIR}/*")
    foreach(_f IN LISTS _candidates)
        get_filename_component(_n "${_f}" NAME)
        string(TOLOWER "${_n}" _nl)
        string(TOLOWER "${name}" _wl)
        if(_nl STREQUAL _wl)
            file(TO_CMAKE_PATH "${_f}" _f)
            set(${out} "${_f}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "${name} not found in EEVAD_PACKAGE_DIR (${EEVAD_PACKAGE_DIR}). Build the driver first.")
endfunction()

eevad_find_file(EEVAD_SYS_FILE "EEVirtualAudio.sys")
eevad_find_file(EEVAD_INF_FILE "EEVirtualAudio.inf")
eevad_find_file(EEVAD_CAT_FILE "EEVirtualAudio.cat")

if(EEVAD_TEST_CERT)
    file(TO_CMAKE_PATH "${EEVAD_TEST_CERT}" _cer)
    set(EEVAD_CER_LINE "IDR_EEVAD_CER RCDATA \"${_cer}\"")
    message(STATUS "EEVirtualAudioSetup: TEST-SIGNED build (embedding ${_cer})")
else()
    set(EEVAD_CER_LINE "")
    message(STATUS "EEVirtualAudioSetup: release build (no test certificate)")
endif()

configure_file(src/setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)

add_executable(EEVirtualAudioSetup
    src/main.cpp
    src/setup.manifest
    "${CMAKE_CURRENT_BINARY_DIR}/setup.rc"
)

target_include_directories(EEVirtualAudioSetup PRIVATE src)
target_compile_definitions(EEVirtualAudioSetup PRIVATE UNICODE _UNICODE)
if(EEVAD_TEST_CERT)
    target_compile_definitions(EEVirtualAudioSetup PRIVATE EEVAD_TEST_SIGNED=1)
endif()

# setup.manifest carries requireAdministrator; stop the linker adding its own asInvoker entry.
target_link_options(EEVirtualAudioSetup PRIVATE /MANIFESTUAC:NO)

# Static CRT so the exe runs on a clean machine without the VC++ redistributable.
set_property(TARGET EEVirtualAudioSetup PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Rebuild when the embedded driver files change.
set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" APPEND PROPERTY OBJECT_DEPENDS
    "${EEVAD_SYS_FILE};${EEVAD_INF_FILE};${EEVAD_CAT_FILE}")
