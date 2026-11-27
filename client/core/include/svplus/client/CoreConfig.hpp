#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::client {

inline constexpr std::size_t   kMaxPlayerSlots       = 1004u;
inline constexpr std::uint32_t kDefaultMenuHotkey    = 0x7Au; // VK_F11
inline constexpr int           kDefaultLocalVolume   = 100;
inline constexpr int           kDefaultGlobalVolume  = 100;
inline constexpr int           kMaxChannelVolume     = 200;
inline constexpr int           kDefaultPlayerVolume  = 100;
inline constexpr int           kMaxPlayerVolume      = 200;
inline constexpr int           kDefaultMicroGain     = 100;
inline constexpr int           kMaxMicroGain         = 200;

enum class TalkMode : std::uint8_t {
    PushToTalk = 0,
    Toggle     = 1,
};

enum class BlacklistScope : std::uint8_t {
    Server = 0, // Active only on origin server (serverEndpoint)
    All    = 1, // Active across all servers
};

struct BlacklistEntry {
    std::string    playerName{};
    std::string    serverName{};
    std::string    serverEndpoint{};
    BlacklistScope scope{BlacklistScope::Server};
};

struct PlayerVolumeEntry {
    std::string playerName{};
    std::string serverName{};
    std::string serverEndpoint{};
    int         volume{kDefaultPlayerVolume};
};

/// File-free portable configuration consumed by CaptureEngine, PlaybackEngine, and VoiceClient.
struct CoreConfig {
    std::uint32_t menuHotkey{kDefaultMenuHotkey};

    // Audio playback
    bool soundEnable{true};
    int  soundVolume{100};                  // 0 .. 100
    int  localVolume{kDefaultLocalVolume};  // 0 .. 200
    int  globalVolume{kDefaultGlobalVolume}; // 0 .. 200
    bool soundBalancer{false};              // compressor
    bool soundFilter{false};                // high-pass / speech clarity filter

    // Microphone capture
    bool        microEnable{true};
    int         microGain{kDefaultMicroGain}; // 0 .. 200 (%)
    std::string microDevice{};
    bool        microNoiseSuppress{true};
    TalkMode    talkMode{TalkMode::PushToTalk};

    // Scoped player block & per-player volume lists
    std::vector<BlacklistEntry> blacklistEntries{};
    std::uint32_t               muteGeneration{1};

    std::vector<PlayerVolumeEntry> playerVolumeEntries{};
    std::uint32_t                  volumeGeneration{1};

    [[nodiscard]] const BlacklistEntry* findOriginEntry(std::string_view name, std::string_view originEndpoint) const noexcept;
    [[nodiscard]] const BlacklistEntry* findBlockingEntry(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
    [[nodiscard]] bool isPlayerMuted(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
    [[nodiscard]] int  getPlayerVolumePct(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
};

} // namespace svplus::client
