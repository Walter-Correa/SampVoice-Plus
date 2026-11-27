add_library(svplus_mobile SHARED
    include/svplus_mobile.h
    src/MobilePlatform.hpp
    src/MobilePlatform.cpp
    src/SvPlusMobile.cpp
    svplus_mobile.version
)

add_library(svplus::mobile ALIAS svplus_mobile)

target_include_directories(svplus_mobile
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(svplus_mobile PRIVATE cxx_std_20)
svplus_set_warnings(svplus_mobile)

if(WIN32)
    target_compile_definitions(svplus_mobile PRIVATE SVPLUS_MOBILE_EXPORTS)
endif()

set_target_properties(svplus_mobile PROPERTIES
    OUTPUT_NAME "svplus_mobile"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/mobile"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/mobile"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/mobile"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/mobile"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/mobile"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/mobile"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_link_libraries(svplus_mobile
    PRIVATE
        svplus::client_core
)

if(ANDROID)
    target_compile_options(svplus_mobile PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(svplus_mobile PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/svplus_mobile.version"
        "-Wl,--exclude-libs,ALL"
        "-Wl,-z,max-page-size=16384"
        "-Wl,--gc-sections"
        "-Wl,--build-id"
    )
    target_link_libraries(svplus_mobile PRIVATE dl log)
elseif(UNIX AND NOT APPLE)
    target_compile_options(svplus_mobile PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(svplus_mobile PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/svplus_mobile.version"
        "-Wl,--exclude-libs,ALL"
        "-Wl,--gc-sections"
        "-Wl,--build-id"
    )
    target_link_libraries(svplus_mobile PRIVATE dl)
endif()
