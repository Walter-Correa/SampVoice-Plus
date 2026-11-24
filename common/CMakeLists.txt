add_library(svplus_common STATIC
    src/Crc32c.cpp
    src/Logger.cpp
    src/UdpSocket.cpp
)

add_library(svplus::common ALIAS svplus_common)

target_include_directories(svplus_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated/include
)

target_compile_features(svplus_common PUBLIC cxx_std_20)
svplus_set_warnings(svplus_common)

if(WIN32)
    target_link_libraries(svplus_common PUBLIC ws2_32)
endif()
