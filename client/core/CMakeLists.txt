add_library(svplus_client_core STATIC
    include/svplus/client/ActiveSpeakerQueue.hpp
    include/svplus/client/AudioStream.hpp
    include/svplus/client/BassApi.hpp
    include/svplus/client/CaptureEngine.hpp
    include/svplus/client/CoreConfig.hpp
    include/svplus/client/OutgoingControlQueue.hpp
    include/svplus/client/Platform.hpp
    include/svplus/client/PlaybackEngine.hpp
    include/svplus/client/Utf8Util.hpp
    include/svplus/client/VoiceClient.hpp
    include/svplus/client/VoiceCodec.hpp
    src/AudioStream.cpp
    src/CaptureEngine.cpp
    src/CoreConfig.cpp
    src/PlaybackEngine.cpp
    src/VoiceClient.cpp
    src/VoiceCodec.cpp
)

add_library(svplus::client_core ALIAS svplus_client_core)

target_include_directories(svplus_client_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(svplus_client_core SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/bass")

target_compile_features(svplus_client_core PUBLIC cxx_std_20)
svplus_set_warnings(svplus_client_core)

target_link_libraries(svplus_client_core
    PUBLIC
        svplus::common
        opus
)
