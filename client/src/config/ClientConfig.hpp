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

inline constexpr float kDefaultMicroIconScale         = 1.25f;
inline constexpr float kDefaultSpeakerIconScale       = 1.0f;
inline constexpr float kDefaultSpeakerListScale       = 1.0f;
inline constexpr int   kDefaultOverheadIconBgOpacity  = 55;
inline constexpr int   kDefaultOverheadIconHeight     = 0;
inline constexpr int   kDefaultSpeakerListBgOpacity   = 45;
inline constexpr int   kDefaultSpeakerListMax         = 5;

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

struct ClientConfig {
    // UI & Localization
    std::uint32_t menuHotkey{kDefaultMenuHotkey};
    std::string   language{"en"};
    float         menuX{-1.0f}; // Normalized [0..1] screen fraction (< 0 = centered on first open)
    float         menuY{-1.0f};
    float         menuW{-1.0f}; // Normalized [0..1] screen fraction (< 0 = 1002x666 @ 1080p scaled)
    float         menuH{-1.0f};

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
    bool soundEnable{true};
    int  soundVolume{100};     // 0 .. 100
    bool soundBalancer{false}; // compressor
    bool soundFilter{false};   // high-pass / speech clarity filter

    // Microphone capture
    bool        microEnable{true};
    int         microVolume{75}; // 0 .. 100
    std::string microDevice{};
    bool        microNoiseSuppress{true};
    TalkMode    talkMode{TalkMode::PushToTalk};

    // Blacklist entries stored under [blacklist] in sampvoice.ini
    std::vector<BlacklistEntry> blacklistEntries{};
    std::vector<std::string>    invalidBlacklistLines{};
    std::uint32_t               muteGeneration{1};

    [[nodiscard]] bool isPlayerMuted(std::string_view name, std::string_view activeServerEndpoint) const noexcept;
    [[nodiscard]] const BlacklistEntry* findOriginEntry(std::string_view name, std::string_view originEndpoint) const noexcept;
    [[nodiscard]] const BlacklistEntry* findBlockingEntry(std::string_view name, std::string_view activeServerEndpoint) const noexcept;

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
