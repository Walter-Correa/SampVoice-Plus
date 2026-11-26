# ==============================================================================
# SampVoice Plus - Universal Single-File Windows Client (sampvoice.asi)
# ==============================================================================

if(NOT WIN32 OR NOT (CMAKE_SIZEOF_VOID_P EQUAL 4))
    message(STATUS "Skipping sampvoice.asi target (requires 32-bit x86 Windows)")
    return()
endif()

set(TP_DIR "${CMAKE_SOURCE_DIR}/third_party")

# 1. Opus (static, warnings suppressed)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE  OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded")
    set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${TP_DIR}/opus" "${CMAKE_BINARY_DIR}/third_party/opus" EXCLUDE_FROM_ALL)
if(TARGET opus)
    if(MSVC)
        target_compile_options(opus PRIVATE /W0)
    else()
        target_compile_options(opus PRIVATE -w)
    endif()
endif()

# 2. MinHook (static, warnings suppressed)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP_DIR}/minhook" "${CMAKE_BINARY_DIR}/third_party/minhook" EXCLUDE_FROM_ALL)
if(TARGET minhook)
    if(MSVC)
        target_compile_options(minhook PRIVATE /W0)
    else()
        target_compile_options(minhook PRIVATE -w)
    endif()
endif()

# 3. Dear ImGui (v1.91.5 core + Win32 + DX9 backends)
set(IMGUI_DIR "${TP_DIR}/imgui")
add_library(svplus_imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx9.cpp"
)
target_include_directories(svplus_imgui SYSTEM PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
if(MSVC)
    target_compile_options(svplus_imgui PRIVATE /W0)
endif()

# 4. RakNet lightweight BitStream / interface sources (from RakHook)
set(RAKNET_DIR "${TP_DIR}/rakhook")
add_library(svplus_raknet STATIC
    "${RAKNET_DIR}/src/RakNet/BitStream.cpp"
    "${RAKNET_DIR}/src/RakNet/DS_HuffmanEncodingTree.cpp"
    "${RAKNET_DIR}/src/RakNet/PluginInterface.cpp"
    "${RAKNET_DIR}/src/RakNet/StringCompressor.cpp"
)
target_include_directories(svplus_raknet SYSTEM PUBLIC
    "${RAKNET_DIR}/include"
    "${RAKNET_DIR}/include/RakNet"
)
if(MSVC)
    target_compile_options(svplus_raknet PRIVATE /W0)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
    foreach(_tp_target IN ITEMS opus minhook svplus_imgui svplus_raknet)
        if(TARGET ${_tp_target})
            set_target_properties(${_tp_target} PROPERTIES SYSTEM ON)
        endif()
    endforeach()
endif()

# 5. Final standalone sampvoice.asi module with .rc embedded UI resources
add_library(sampvoice SHARED
    resources/ResourceIds.hpp
    resources/resources.rc
    src/PluginMain.cpp
    src/ShutdownState.hpp
    src/assets/EmbeddedAssets.cpp
    src/assets/EmbeddedAssets.hpp
    src/audio/AudioStream.cpp
    src/audio/AudioStream.hpp
    src/audio/BassLoader.cpp
    src/audio/BassLoader.hpp
    src/audio/CaptureEngine.cpp
    src/audio/CaptureEngine.hpp
    src/audio/PlaybackEngine.cpp
    src/audio/PlaybackEngine.hpp
    src/audio/VoiceCodec.cpp
    src/audio/VoiceCodec.hpp
    src/config/ClientConfig.cpp
    src/config/ClientConfig.hpp
    src/diag/ModDiagnostics.cpp
    src/diag/ModDiagnostics.hpp
    src/net/VoiceClient.cpp
    src/net/VoiceClient.hpp
    src/samp/RakClientInterfaceStubs.cpp
    src/samp/SampBridge.cpp
    src/samp/SampBridge.hpp
    src/samp/VersionDetector.hpp
    src/ui/ConfigMenu.cpp
    src/ui/ConfigMenu.hpp
    src/ui/D3D9Hook.cpp
    src/ui/D3D9Hook.hpp
    src/ui/HudOverlay.cpp
    src/ui/HudOverlay.hpp
    src/ui/Localization.cpp
    src/ui/Localization.hpp
    src/ui/MenuLayout.cpp
    src/ui/MenuLayout.hpp
    src/util/Utf8Util.hpp
)

target_include_directories(sampvoice
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    SYSTEM PRIVATE
        "${TP_DIR}/bass"
        "${TP_DIR}/stb"
)

target_compile_features(sampvoice PRIVATE cxx_std_20)
target_compile_definitions(sampvoice PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
svplus_set_warnings(sampvoice)

target_link_libraries(sampvoice
    PRIVATE
        svplus::common
        svplus_imgui
        svplus_raknet
        opus
        minhook
        d3d9
        ws2_32
        winmm
        shell32
        bcrypt
        version
        psapi
)

set_target_properties(sampvoice PROPERTIES
    OUTPUT_NAME "sampvoice"
    PREFIX ""
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/client"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/client"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/client"
)
