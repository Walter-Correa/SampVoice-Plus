#pragma once

#include "audio/ActiveSpeakerQueue.hpp"
#include "config/ClientConfig.hpp"
#include "net/VoiceClient.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/Localization.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace svplus::client::ui {

class HudOverlay {
public:
    static HudOverlay& instance() noexcept;

    void setSaMpConfigPath(const std::filesystem::path& cfgPath);

    void render(config::ClientConfig& cfg,
                const Localization& loc,
                const UiTextures& textures,
                bool menuOpen,
                bool& outConfigDirty);

    void requestResetMicroPosition() noexcept { forceResetMicroPos_ = true; }
    void requestResetSpeakerListPosition() noexcept { forceResetSpeakerListPos_ = true; }

    /// Computes SA-MP nametag vertical offset `hSamp` from RsGlobal screen width and `sa-mp.cfg` `fontsize`.
    [[nodiscard]] static float computeSampNametagHeight(int screenWidthPx, int saMpFontSize) noexcept;

    /// Computes final 3D overhead icon center Y so bottom edge sits at `nameTop - 3 - height*distScale`.
    [[nodiscard]] static float computeOverheadIconCenterY(
        float projectedY,
        float hSamp,
        float outerHalfHeight,
        int   overheadHeightPx,
        float distScale) noexcept;

    /// Converts wire 0xAARRGGBB color to ImGui IM_COL32 with alpha forced to 255.
    [[nodiscard]] static std::uint32_t argbToImU32FullAlpha(std::uint32_t argb) noexcept;

    /// Tracks whether local TX or its 3-second post-release overhead voice-status window is active.
    [[nodiscard]] bool updateVoiceStatusOverheadWindow(
        bool voiceConnected,
        bool transmittingNow,
        std::chrono::steady_clock::time_point now) noexcept;

    /// Renders speaker list overlay from explicit local/remote view (shared by live HUD and offline visual verification).
    void renderSpeakerListWithState(config::ClientConfig& cfg,
                                    const Localization& loc,
                                    const UiTextures& textures,
                                    bool localTalking,
                                    std::uint16_t localId,
                                    std::string_view localName,
                                    std::uint32_t localColorArgb,
                                    std::span<const net::LocalSpeakerStreamTag> localStreams,
                                    bool serverIsPlus,
                                    const std::vector<audio::ActiveSpeakerInfo>& speakers,
                                    const std::function<std::uint32_t(std::uint16_t)>& resolvePlayerColor,
                                    bool menuOpen,
                                    bool& dirty,
                                    const std::function<bool(std::uint32_t)>& isGlobalStreamJoined = {});

    void renderMicroStatusWithState(config::ClientConfig& cfg,
                                    const UiTextures& textures,
                                    bool showMic,
                                    bool transmitting,
                                    bool micMuted,
                                    bool deafened,
                                    bool menuOpen,
                                    bool& dirty);

private:
    HudOverlay() = default;
    ~HudOverlay() = default;
    HudOverlay(const HudOverlay&) = delete;
    HudOverlay& operator=(const HudOverlay&) = delete;

    void refreshSaMpFontSizeIfNeeded() noexcept;

    void renderMicroIcon(config::ClientConfig& cfg,
                         const UiTextures& textures,
                         bool showMic,
                         bool deafened,
                         bool menuOpen,
                         bool& dirty);

    void renderSpeakerList(config::ClientConfig& cfg,
                           const Localization& loc,
                           const UiTextures& textures,
                           const std::vector<audio::ActiveSpeakerInfo>& speakers,
                           bool menuOpen,
                           bool& dirty);

    void renderOverhead3DIcons(const config::ClientConfig& cfg,
                               const UiTextures& textures,
                               const std::vector<audio::ActiveSpeakerInfo>& speakers);

    std::filesystem::path          saMpCfgPath_{};
    bool                           hasCfgMtime_{false};
    std::filesystem::file_time_type lastCfgMtime_{};
    std::uint64_t                  lastCfgCheckMs_{0};
    int                            cachedSaMpFontSize_{0};
    bool                           loggedInitialFontsize_{false};

    bool forceResetMicroPos_{false};
    bool forceResetSpeakerListPos_{false};
    bool prevMenuOpen_{false};
    bool draggingMicro_{false};
    bool draggingSpeakerList_{false};
    bool txPersistenceActive_{false};
    std::chrono::steady_clock::time_point lastLocalTxTime_{};
};

} // namespace svplus::client::ui
