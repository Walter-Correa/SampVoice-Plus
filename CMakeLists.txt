cmake_minimum_required(VERSION 3.20)

project(SampVoicePlus
    VERSION 1.5.0
    DESCRIPTION "High-performance 32-bit voice chat SDK for SA-MP and open.mp"
    LANGUAGES CXX C
)

option(SVPLUS_BUILD_SERVER       "Build server core and host adapters (open.mp & SA-MP 0.3.7)" ON)
option(SVPLUS_BUILD_CLIENT       "Build universal Windows client (sampvoice.asi)" ON)
option(SVPLUS_BUILD_CLIENT_CORE  "Build portable client voice core library (svplus_client_core)" OFF)
option(SVPLUS_BUILD_MOBILE       "Build mobile client C SDK shared library (libsvplus_mobile.so)" OFF)
option(SVPLUS_STATIC_CRT         "Link MSVC C/C++ runtime statically (/MT)" ON)
option(SVPLUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on SampVoice Plus targets" OFF)

if(SVPLUS_BUILD_MOBILE AND NOT SVPLUS_BUILD_CLIENT_CORE)
    message(FATAL_ERROR "SVPLUS_BUILD_MOBILE requires SVPLUS_BUILD_CLIENT_CORE=ON.")
endif()

if((SVPLUS_BUILD_SERVER OR SVPLUS_BUILD_CLIENT) AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "SampVoice Plus strictly targets 32-bit x86 (Windows Win32 and Linux -m32) to match "
        "GTA San Andreas / SA-MP 0.3.7 and open.mp server architectures.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-source-of-truth generated Version.hpp and VersionRc.h
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include/svplus/common/Version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/include/svplus/common/Version.hpp"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include/svplus/common/VersionRc.h.in"
    "${CMAKE_BINARY_DIR}/generated/include/svplus/common/VersionRc.h"
    @ONLY
)

if(SVPLUS_BUILD_MOBILE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(MSVC AND SVPLUS_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /utf-8 /MP /external:anglebrackets /external:W0)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-fPIC)
endif()

function(svplus_set_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if(SVPLUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(SVPLUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(common)

if(SVPLUS_BUILD_SERVER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/omp-sdk/CMakeLists.txt" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/omp-network/CMakeLists.txt")
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        add_subdirectory(third_party/omp-sdk)
        add_subdirectory(third_party/omp-network)
        if(MSVC)
            target_compile_options(OMP-Network PRIVATE /W0)
        else()
            target_compile_options(OMP-Network PRIVATE -w)
        endif()
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
            foreach(_tp_target IN ITEMS OMP-SDK OMP-Network glm robin_hood span-lite string-view-lite)
                if(TARGET ${_tp_target})
                    set_target_properties(${_tp_target} PROPERTIES SYSTEM ON)
                endif()
            endforeach()
        endif()
    endif()
    add_subdirectory(server)
endif()

if(SVPLUS_BUILD_CLIENT OR SVPLUS_BUILD_CLIENT_CORE OR SVPLUS_BUILD_MOBILE)
    add_subdirectory(client)
endif()
