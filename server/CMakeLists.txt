add_library(svplus_server_core STATIC
    src/Effect.cpp
    src/PlayerStore.cpp
    src/ServerCore.cpp
    src/SpatialGrid.cpp
    src/Stream.cpp
)

add_library(svplus::server_core ALIAS svplus_server_core)

target_include_directories(svplus_server_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(svplus_server_core
    PUBLIC
        svplus::common
)

target_compile_features(svplus_server_core PUBLIC cxx_std_20)
set_target_properties(svplus_server_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
svplus_set_warnings(svplus_server_core)
if(MSVC)
    # Keep std::atomic_load/store_explicit<shared_ptr> on server core for GCC 10 (bullseye) compat
    target_compile_definitions(svplus_server_core PRIVATE _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
endif()

# Third-party Pawn SDK dispatch helper kept in its own target so third-party /w does not collide
# with /W4 (D9025) or trigger warnings on SampVoice Plus sources.
add_library(svplus_pawn_amxplugin STATIC
    "${PROJECT_SOURCE_DIR}/third_party/pawn-sdk/amxplugin.cpp"
)
target_include_directories(svplus_pawn_amxplugin
    SYSTEM PUBLIC
        "${PROJECT_SOURCE_DIR}/third_party/pawn-sdk"
)
set_target_properties(svplus_pawn_amxplugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(svplus_pawn_amxplugin PRIVATE /W0)
else()
    target_compile_options(svplus_pawn_amxplugin PRIVATE -w)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
    set_target_properties(svplus_pawn_amxplugin PROPERTIES SYSTEM ON)
endif()

# Shared Pawn AMX native bindings (used by both open.mp and legacy SA-MP adapters)
add_library(svplus_pawn_bindings STATIC
    adapters/common/PawnBindings.cpp
)

add_library(svplus::pawn_bindings ALIAS svplus_pawn_bindings)

target_include_directories(svplus_pawn_bindings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/common
    SYSTEM PUBLIC
        "${PROJECT_SOURCE_DIR}/third_party/pawn-sdk"
)

target_link_libraries(svplus_pawn_bindings
    PUBLIC
        svplus::server_core
        svplus_pawn_amxplugin
)

target_compile_features(svplus_pawn_bindings PUBLIC cxx_std_20)
set_target_properties(svplus_pawn_bindings PROPERTIES POSITION_INDEPENDENT_CODE ON)
svplus_set_warnings(svplus_pawn_bindings)

# open.mp Component (Windows / Linux, 32-bit x86)
if(TARGET OMP-SDK AND TARGET OMP-Network)
    add_library(sampvoice-omp SHARED
        adapters/omp/OmpAdapter.cpp
        adapters/omp/OmpComponent.cpp
    )

    target_include_directories(sampvoice-omp
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/adapters/omp
        SYSTEM PRIVATE
            "${PROJECT_SOURCE_DIR}/third_party/omp-sdk/include"
            "${PROJECT_SOURCE_DIR}/third_party/omp-network"
    )

    target_link_libraries(sampvoice-omp
        PRIVATE
            svplus::pawn_bindings
            OMP-SDK
            OMP-Network
    )
    svplus_set_warnings(sampvoice-omp)

    set_target_properties(sampvoice-omp PROPERTIES
        PREFIX ""
        OUTPUT_NAME "sampvoice"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/omp"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/omp"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/omp"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/omp"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/omp"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/omp"
    )

    if(UNIX AND NOT APPLE)
        target_compile_definitions(sampvoice-omp PRIVATE LINUX)
        target_compile_options(sampvoice-omp PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
        target_link_options(sampvoice-omp PRIVATE
            -static-libgcc
            -static-libstdc++
            -Wl,--exclude-libs,ALL
            "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/adapters/omp/exports_omp.map"
        )
        target_link_libraries(sampvoice-omp PRIVATE pthread dl)
    endif()
endif()

# Legacy SA-MP 0.3.7 Server Plugin (32-bit x86 Windows & Linux)
set(SVPLUS_SAMP_SOURCES
    adapters/samp/SampAdapter.cpp
    adapters/samp/SampLegacyPlugin.cpp
    adapters/samp/SampRakNetHook.cpp
)
if(WIN32)
    list(APPEND SVPLUS_SAMP_SOURCES adapters/samp/sampvoice.def)
endif()

add_library(sampvoice-samp SHARED ${SVPLUS_SAMP_SOURCES})

target_include_directories(sampvoice-samp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/samp
)

target_link_libraries(sampvoice-samp
    PRIVATE
        svplus::pawn_bindings
)
svplus_set_warnings(sampvoice-samp)

set_target_properties(sampvoice-samp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sampvoice"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samp"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/samp"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/samp"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/samp"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/samp"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/samp"
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(sampvoice-samp PRIVATE LINUX)
    target_compile_options(sampvoice-samp PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
    target_link_options(sampvoice-samp PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,--exclude-libs,ALL
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/adapters/samp/exports_plugin.map"
    )
    target_link_libraries(sampvoice-samp PRIVATE pthread dl)
endif()
