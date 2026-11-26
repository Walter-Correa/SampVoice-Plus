#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace svplus::client::ui {

enum class Language : std::uint8_t {
    Id = 0, // Bahasa Indonesia
    De,     // Deutsch
    En,     // English (default)
    Es,     // Español
    Fr,     // Français
    It,     // Italiano
    Hu,     // Magyar
    Pl,     // Polski
    Pt,     // Português
    Ro,     // Română
    Vi,     // Tiếng Việt
    Tr,     // Türkçe
    Bg,     // Български
    Ru,     // Русский
    Uk,     // Українська
    Count
};

enum class TextKey : std::uint16_t {
    WindowTitle = 0,
    TabGeneral,
    TabAudio,
    TabMicrophone,
    TabBlacklist,
    StatusLabel,
    StatusRttMs,
    StatusSampVersion,
    LanguageLabel,
    HotkeyLabel,
    HotkeyPressAny,
    HotkeyResetDefault,
    ShowSpeakerList,
    ShowSpeakerListHeader,
    ShowSpeakerListBg,
    SpeakerListBgOpacity,
    SpeakerListMax,
    SpeakerListScale,
    ResetSpeakerListPosition,
    ShowMicroIcon,
    MicroIconScale,
    ResetMicroPosition,
    ShowOverheadIcons,
    OverheadIconBgOpacity,
    SpeakerIconScale,
    ResetButton,
    SoundEnable,
    SoundVolume,
    SoundBalancer,
    SoundFilter,
    MicroEnable,
    MicroDevice,
    MicroDeviceDefault,
    MicroVolume,
    MicroNoiseSuppress,
    MicroTestLevel,
    MicroResetDevice,
    TalkModeLabel,
    TalkModePush,
    TalkModeToggle,
    HotkeyConflictWarning,
    DiagIdle,
    DiagConnecting,
    DiagNoVoiceServer,
    DiagUdpBlocked,
    DiagConnectedMicDisabled,
    DiagConnectedNoMic,
    DiagConnectedMuted,
    DiagListenerDisabled,
    DiagConnectedOk,
    BlacklistSearchPlaceholder,
    BlacklistPlayersOnline,
    BlacklistMutedList,
    BlacklistMuteButton,
    BlacklistUnmuteButton,
    BlacklistClearAll,
    BlacklistEmpty,
    SpeakerListHeader,
    FooterHint,
    OverheadIconHeight,
    BlacklistDisconnected,
    BlacklistCurrentServerHeader,
    BlacklistMuteHereBtn,
    BlacklistMuteAllBtn,
    BlacklistUnmuteHereButton,
    BlacklistMutedAllFromFmt,
    BlacklistAllHeader,
    BlacklistAllSearchPlaceholder,
    BlacklistColPlayer,
    BlacklistColServer,
    BlacklistColEndpoint,
    BlacklistColScope,
    BlacklistColActions,
    BlacklistScopeOriginBtn,
    BlacklistScopeAllLabel,
    BlacklistScopeServerLabel,
    BlacklistRemoveBtn,
    SpeakerBlockedTag,
    BlacklistFilterAll,
    BlacklistFilterNearby,
    VoiceStatusNoMicBadge,
    VoiceStatusNoHearBadge,
    VoiceStatusNoMicTooltip,
    VoiceStatusNoHearTooltip,
    BlacklistNearbyEmpty,
    Count
};

struct LanguageInfo {
    Language    id;
    const char* code;
    const char* nativeName;
};

class Localization {
public:
    static constexpr std::size_t kLanguageCount = static_cast<std::size_t>(Language::Count);
    static constexpr std::size_t kKeyCount      = static_cast<std::size_t>(TextKey::Count);

    [[nodiscard]] static std::span<const LanguageInfo> languages() noexcept;
    [[nodiscard]] static Language parseCode(std::string_view code) noexcept;
    [[nodiscard]] static const char* codeOf(Language lang) noexcept;
    [[nodiscard]] static const char* nativeNameOf(Language lang) noexcept;

    [[nodiscard]] static const char* get(Language lang, TextKey key) noexcept;

    void setActiveLanguage(Language lang) noexcept { active_ = lang; }
    [[nodiscard]] Language activeLanguage() const noexcept { return active_; }
    [[nodiscard]] const char* tr(TextKey key) const noexcept { return get(active_, key); }

private:
    Language active_{Language::En};
};

} // namespace svplus::client::ui
