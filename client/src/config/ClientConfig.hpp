#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::client::config {

inline constexpr std::uint32_t kDefaultMenuHotkey = 0x7A; // VK_F11

// Default HUD positions normalized as [0..1] screen fractions (legacy SV bottom-left above radar;
// matches 1920x1080 pixels (291, 746) for mic icon and (42, 426) for speaker list).
inline constexpr float kDefaultMicroIconX   = 0.1516f;
inline constexpr float kDefaultMicroIconY   = 0.6907f;
inline constexpr float kDefaultSpeakerListX = 0.0219f;
inline constexpr float kDefaultSpeakerListY = 0.3944f;

inline constexpr float kMinMicroIconScale             = 0.5f;
inline constexpr float kDefaultMicroIconScale         = 1.25f;
inline constexpr float kMaxMicroIconScale             = 2.5f;

inline constexpr float kMinSpeakerIconScale           = 0.5f;
inline constexpr float kDefaultSpeakerIconScale       = 1.0f;
inline constexpr float kMaxSpeakerIconScale           = 2.5f;

inline constexpr float kMinSpeakerListScale           = 0.75f;
inline constexpr float kDefaultSpeakerListScale       = 1.0f;
inline constexpr float kMaxSpeakerListScale           = 2.0f;
inline constexpr int   kDefaultOverheadIconBgOpacity  = 55;
inline constexpr int   kDefaultOverheadIconHeight     = 0;
inline constexpr int   kDefaultSpeakerListBgOpacity   = 45;
inline constexpr int   kDefaultSpeakerListMax         = 5;
inline constexpr int   kDefaultLocalVolume            = 100;
inline constexpr int   kDefaultGlobalVolume           = 100;
inline constexpr int   kMaxChannelVolume              = 200;
inline constexpr std::uint32_t kDefaultDeafenHotkey   = 0;
inline constexpr int   kDefaultPlayerVolume           = 100;
inline constexpr int   kMaxPlayerVolume               = 200;
inline constexpr int   kDefaultMicroGain              = 100;
inline constexpr int   kMaxMicroGain                  = 200;

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

struct ClientConfig {
    // UI & Localization
    std::uint32_t menuHotkey{kDefaultMenuHotkey};
    std::string   language{"en"};
    float         menuX{-1.0f}; // Normalized [0..1] screen fraction (< 0 = centered on first open)
    float         menuY{-1.0f};
    float         menuW{-1.0f}; // Normalized [0..1] screen fraction (used only when menuSizeManual == true)
    float         menuH{-1.0f};
    bool          menuSizeManual{false};

    bool          showSpeakerList{true};
    bool          speakerListHeader{true};
    bool          speakerListBg{true};
    int           speakerListBgOpacity{kDefaultSpeakerListBgOpacity}; // 0 .. 100
    int           speakerListMax{kDefaultSpeakerListMax};             // 1 .. 15
    float         speakerListScale{kDefaultSpeakerListScale};
    float         speakerListX{kDefaultSpeakerListX};
    float         speakerListY{kDefaultSpeakerListY};

    bool          showMicroIcon{true};
    float         microIconScale{kDefaultMicroIconScale};
    float         microIconX{kDefaultMicroIconX};
    float         microIconY{kDefaultMicroIconY};

    bool          showOverheadIcons{true};
    int           overheadIconBgOpacity{kDefaultOverheadIconBgOpacity}; // 0 .. 100
    int           overheadIconHeight{kDefaultOverheadIconHeight};       // 0 .. 60 px above nametag
    float         speakerIconScale{kDefaultSpeakerIconScale};

    // Audio playback
    bool          soundEnable{true};
    int           soundVolume{100};                  // 0 .. 100
    int           localVolume{kDefaultLocalVolume};   // 0 .. 200
    int           globalVolume{kDefaultGlobalVolume}; // 0 .. 200
    bool          soundBalancer{false};              // compressor
    bool          soundFilter{false};                // high-pass / speech clarity filter
    std::uint32_t deafenHotkey{kDefaultDeafenHotkey}; // 0 = none

    // Microphone capture
    bool        microEnable{true};
    int         microGain{kDefaultMicroGain}; // 0 .. 200 (%)
    std::string microDevice{};
    bool        microNoiseSuppress{true};
    TalkMode    talkMode{TalkMode::PushToTalk};

    // Blacklist entries stored under [blacklist] in sampvoice.ini
    std::vector<BlacklistEntry> blacklistEntries{};
    std::vector<std::string>    invalidBlacklistLines{};
    std::uint32_t               muteGeneration{1};

    // Per-player volume entries stored under [player_volume] in sampvoice.ini (0..200%, 100 omitted)
    std::vector<PlayerVolumeEntry> playerVolumeEntries{};
    std::vector<std::string>       invalidPlayerVolumeLines{};
    std::uint32_t                  volumeGeneration{1};

    [[nodiscard]] bool isPlayerMuted(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
    [[nodiscard]] const BlacklistEntry* findOriginEntry(std::string_view name, std::string_view originEndpoint) const noexcept;
    [[nodiscard]] const BlacklistEntry* findBlockingEntry(std::string_view name, std::string_view activeServerEndpoint) const noexcept;

    [[nodiscard]] int getPlayerVolumePct(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
    bool setPlayerVolumePct(std::string_view name,
                            std::string_view serverName,
                            std::string_view serverEndpoint,
                            int              volumePct,
                            bool             logChange = true,
                            bool             removeIfDefault = true);

    bool mutePlayer(std::string_view name,
                    std::string_view serverName,
                    std::string_view serverEndpoint,
                    BlacklistScope   scope);
    bool removeBlacklistEntry(std::string_view name, std::string_view originEndpoint);
    bool setBlacklistEntryScope(std::string_view name, std::string_view originEndpoint, BlacklistScope scope);
    std::size_t clearOriginServerBlacklist(std::string_view originEndpoint);
    bool refreshServerNameForEndpoint(std::string_view originEndpoint, std::string_view serverName);

    [[nodiscard]] bool loadFromFile(const std::filesystem::path& path);
    [[nodiscard]] bool saveToFile(const std::filesystem::path& path) const;

    void logLoadedSummary(const std::filesystem::path& path) const;
    void logConfigDiff(const ClientConfig& previous) const;
};

/// Human-readable name for a Win32 virtual key code (e.g. 0x7A -> "F11").
[[nodiscard]] std::string formatVirtualKeyName(std::uint32_t vk) noexcept;

/// Converts legacy pixel coordinates (`coord > 1.0f`) or sentinel (`coord < 0.0f`) to a [0..1]
/// screen fraction using `displayExtent`, setting `dirty = true` when migrated.
[[nodiscard]] float resolveHudAxisFraction(float& coord, float displayExtent, float defaultFraction, bool& dirty) noexcept;

} // namespace svplus::client::config
