#include "ui/HudOverlay.hpp"

#include "audio/CaptureEngine.hpp"
#include "audio/PlaybackEngine.hpp"
#include "net/VoiceClient.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"
#include "ui/D3D9Hook.hpp"

#include <imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string_view>

namespace svplus::client::ui {
namespace {

constexpr std::uintptr_t kRsGlobalScreenWidthAddr = 0xC17044u;

int tryReadRsGlobalScreenWidth(int fallbackWidth) noexcept {
    int width = 0;
    __try {
        width = *reinterpret_cast<const int*>(kRsGlobalScreenWidthAddr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        width = 0;
    }
    return (width >= 320 && width <= 16384) ? width : (std::max)(320, fallbackWidth);
}

int parseSaMpFontSizeFromCfg(const std::filesystem::path& cfgPath) {
    std::ifstream in(cfgPath, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return 0;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::size_t s = 0;
        while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
        if (s >= line.size() || line[s] == ';' || line[s] == '#') continue;
        const std::string_view sv(line.data() + s, line.size() - s);
        if (sv.size() >= 8 && _strnicmp(sv.data(), "fontsize", 8) == 0) {
            std::size_t eq = sv.find('=', 8);
            if (eq == std::string_view::npos) continue;
            std::size_t v = eq + 1;
            while (v < sv.size() && (sv[v] == ' ' || sv[v] == '\t')) ++v;
            int parsed = 0;
            if (v < sv.size()) {
                char* endp = nullptr;
                const long raw = std::strtol(sv.data() + v, &endp, 10);
                if (endp != sv.data() + v) {
                    parsed = std::clamp(static_cast<int>(raw), -3, 10);
                }
            }
            return parsed;
        }
    }
    return 0;
}

void enableMipLinearCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    if (auto* device = static_cast<IDirect3DDevice9*>(cmd->UserCallbackData)) {
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    }
}

void disableMipFilterCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    if (auto* device = static_cast<IDirect3DDevice9*>(cmd->UserCallbackData)) {
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    }
}

void drawIconWithMipFilter(IDirect3DTexture9* tex, const ImVec2& size, IDirect3DDevice9* device) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (device && draw) {
        draw->AddCallback(&enableMipLinearCallback, device);
    }
    ImGui::Image(reinterpret_cast<ImTextureID>(tex), size);
    if (device && draw) {
        draw->AddCallback(&disableMipFilterCallback, device);
    }
}

struct ColoredSegment {
    std::string text;
    ImU32       color;
};

void drawSpeakerListRow(IDirect3DTexture9*              iconTex,
                        IDirect3DDevice9*               device,
                        float                           scale,
                        const std::vector<ColoredSegment>& segments) {
    if (segments.empty()) {
        return;
    }
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float glyphScale = (font && font->FontSize > 0.0f) ? (fontSize / font->FontSize) : 1.0f;
    const ImFontGlyph* glyphH = font ? font->FindGlyph('H') : nullptr;
    const float hCenterLocal = glyphH
        ? ((glyphH->Y0 + glyphH->Y1) * 0.5f * glyphScale)
        : (fontSize * 0.5f);

    const float outlinePx = (std::max)(1.0f, 1.0f * scale);
    const float iconSize  = 16.0f * scale;
    const float iconGap   = iconTex ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
    const float textH     = ImGui::GetTextLineHeight();

    float totalTextW = 0.0f;
    std::vector<float> segWidths;
    segWidths.reserve(segments.size());
    for (const auto& seg : segments) {
        const float w = (!seg.text.empty())
            ? (font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, seg.text.c_str()).x
                    : ImGui::CalcTextSize(seg.text.c_str()).x)
            : 0.0f;
        segWidths.push_back(w);
        totalTextW += w;
    }

    const float centerYLocal = iconTex
        ? (std::max)(iconSize * 0.5f, outlinePx + hCenterLocal)
        : (outlinePx + hCenterLocal);
    const float iconTopLocal = centerYLocal - iconSize * 0.5f;
    const float textTopLocal = centerYLocal - hCenterLocal;
    const float rowH = (std::max)(
        iconTex ? (centerYLocal + iconSize * 0.5f) : 0.0f,
        textTopLocal + textH + outlinePx);
    const float rowW = (iconTex ? (iconSize + iconGap) : 0.0f) + totalTextW + outlinePx * 2.0f;

    const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(rowW, rowH));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!dl) {
        return;
    }

    if (iconTex) {
        if (device) dl->AddCallback(&enableMipLinearCallback, device);
        dl->AddImage(
            reinterpret_cast<ImTextureID>(iconTex),
            ImVec2(rowOrigin.x, rowOrigin.y + iconTopLocal),
            ImVec2(rowOrigin.x + iconSize, rowOrigin.y + iconTopLocal + iconSize));
        if (device) dl->AddCallback(&disableMipFilterCallback, device);
    }

    constexpr float kDirs[8][2] = {
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
        {-1.0f,  0.0f},                {1.0f,  0.0f},
        {-1.0f,  1.0f}, {0.0f,  1.0f}, {1.0f,  1.0f},
    };

    float curX = rowOrigin.x + (iconTex ? (iconSize + iconGap) : 0.0f) + outlinePx;
    const float textY = rowOrigin.y + textTopLocal;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        if (!seg.text.empty()) {
            const ImU32 fgCol = seg.color | (0xFFu << IM_COL32_A_SHIFT);
            const ImU32 outCol = IM_COL32(0, 0, 0, 255);
            for (const auto& d : kDirs) {
                dl->AddText(
                    font,
                    fontSize,
                    ImVec2(curX + d[0] * outlinePx, textY + d[1] * outlinePx),
                    outCol,
                    seg.text.c_str());
            }
            dl->AddText(font, fontSize, ImVec2(curX, textY), fgCol, seg.text.c_str());
        }
        curX += segWidths[i];
    }
}

void drawGrabHandCursor(ImDrawList* fg, ImVec2 pos) noexcept {
    if (!fg) {
        return;
    }
    const ImU32 fill   = IM_COL32(255, 255, 255, 255);
    const ImU32 border = IM_COL32(20, 24, 31, 255);

    const ImVec2 palmMin(pos.x - 6.0f, pos.y - 3.0f);
    const ImVec2 palmMax(pos.x + 7.0f, pos.y + 8.0f);
    fg->AddRectFilled(palmMin, palmMax, fill, 4.0f);
    fg->AddRect(palmMin, palmMax, border, 4.0f, 0, 1.5f);

    for (int i = 0; i < 4; ++i) {
        const float fx = pos.x - 5.2f + i * 3.1f;
        const ImVec2 fMin(fx, pos.y - 7.0f);
        const ImVec2 fMax(fx + 2.8f, pos.y - 1.5f);
        fg->AddRectFilled(fMin, fMax, fill, 1.4f);
        fg->AddRect(fMin, fMax, border, 1.4f, 0, 1.2f);
    }

    const ImVec2 tMin(pos.x - 9.0f, pos.y - 1.0f);
    const ImVec2 tMax(pos.x - 4.5f, pos.y + 5.0f);
    fg->AddRectFilled(tMin, tMax, fill, 2.0f);
    fg->AddRect(tMin, tMax, border, 2.0f, 0, 1.2f);
}

bool updateHudDragCursor(bool menuOpen, bool& draggingFlag) noexcept {
    if (!menuOpen) {
        draggingFlag = false;
        return false;
    }

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!leftDown) {
        draggingFlag = false;
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggingFlag = true;
    }

    if (draggingFlag) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        drawGrabHandCursor(ImGui::GetForegroundDrawList(), ImGui::GetMousePos());
        return true;
    }
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    return false;
}

} // namespace

HudOverlay& HudOverlay::instance() noexcept {
    static auto* inst = new HudOverlay();
    return *inst;
}

void HudOverlay::setSaMpConfigPath(const std::filesystem::path& cfgPath) {
    saMpCfgPath_ = cfgPath;
    hasCfgMtime_ = false;
    lastCfgCheckMs_ = 0;
    loggedInitialFontsize_ = false;
}

float HudOverlay::computeSampNametagHeight(int screenWidthPx, int saMpFontSize) noexcept {
    int base = 14;
    if (screenWidthPx >= 1600) {
        base = 20;
    } else if (screenWidthPx >= 1400) {
        base = 18;
    } else if (screenWidthPx >= 1024) {
        base = 16;
    }
    return static_cast<float>(base + 2 * saMpFontSize - 2);
}

float HudOverlay::computeOverheadIconCenterY(
    float projectedY,
    float hSamp,
    float outerHalfHeight,
    int   overheadHeightPx,
    float distScale) noexcept {
    const float nameTop = projectedY + 17.0f - hSamp;
    const float extraPx = static_cast<float>(std::clamp(overheadHeightPx, 0, 60)) * distScale;
    return nameTop - 3.0f - extraPx - outerHalfHeight;
}

std::uint32_t HudOverlay::argbToImU32FullAlpha(std::uint32_t argb) noexcept {
    const std::uint32_t r = (argb >> 16u) & 0xFFu;
    const std::uint32_t g = (argb >> 8u) & 0xFFu;
    const std::uint32_t b = argb & 0xFFu;
    return IM_COL32(r, g, b, 255u);
}

void HudOverlay::refreshSaMpFontSizeIfNeeded() noexcept {
    if (saMpCfgPath_.empty()) {
        return;
    }
    const std::uint64_t nowMs = GetTickCount64();
    if (lastCfgCheckMs_ != 0 && (nowMs - lastCfgCheckMs_) < 2000u) {
        return;
    }
    lastCfgCheckMs_ = nowMs;

    std::error_code ec;
    if (!std::filesystem::exists(saMpCfgPath_, ec)) {
        return;
    }
    const auto mt = std::filesystem::last_write_time(saMpCfgPath_, ec);
    if (ec) {
        return;
    }
    if (!hasCfgMtime_ || mt != lastCfgMtime_) {
        hasCfgMtime_ = true;
        lastCfgMtime_ = mt;
        const int parsed = parseSaMpFontSizeFromCfg(saMpCfgPath_);
        if (!loggedInitialFontsize_ || parsed != cachedSaMpFontSize_) {
            cachedSaMpFontSize_ = parsed;
            loggedInitialFontsize_ = true;
            SVPLUS_LOG_INF_CAT("render", "Read sa-mp.cfg fontsize=%d", cachedSaMpFontSize_);
        }
    }
}

void HudOverlay::render(config::ClientConfig& cfg,
                        const Localization& loc,
                        const UiTextures& textures,
                        bool menuOpen,
                        bool& outConfigDirty) {
    const auto& samp = samp::SampBridge::instance();
    if (!samp.isConnected() || samp.isGameMenuOpen() || samp.isScoreboardOpen()) {
        draggingMicro_ = false;
        draggingSpeakerList_ = false;
        prevMenuOpen_ = menuOpen;
        return;
    }

    bool dirty = false;
    const auto speakersSnap = (cfg.showSpeakerList || cfg.showOverheadIcons)
        ? audio::PlaybackEngine::instance().activeSpeakersSnapshot()
        : nullptr;
    const std::vector<audio::ActiveSpeakerInfo> emptySpeakers{};
    const auto& speakers = speakersSnap ? *speakersSnap : emptySpeakers;

    if (cfg.showMicroIcon && (menuOpen || samp.isLocalPlayerSpawned())) {
        renderMicroIcon(cfg, textures, menuOpen, dirty);
    } else {
        draggingMicro_ = false;
    }
    if (cfg.showSpeakerList) {
        renderSpeakerList(cfg, loc, textures, speakers, menuOpen, dirty);
    } else {
        draggingSpeakerList_ = false;
    }
    if (cfg.showOverheadIcons) {
        renderOverhead3DIcons(cfg, textures, speakers);
    }

    prevMenuOpen_ = menuOpen;
    if (dirty) {
        outConfigDirty = true;
    }
}

void HudOverlay::renderMicroIcon(config::ClientConfig& cfg,
                                 const UiTextures& textures,
                                 bool menuOpen,
                                 bool& dirty) {
    const auto& io      = ImGui::GetIO();
    const auto& capture = audio::CaptureEngine::instance();

    const float dispW = (std::max)(1.0f, io.DisplaySize.x);
    const float dispH = (std::max)(1.0f, io.DisplaySize.y);
    const float fracX = config::resolveHudAxisFraction(cfg.microIconX, dispW, config::kDefaultMicroIconX, dirty);
    const float fracY = config::resolveHudAxisFraction(cfg.microIconY, dispH, config::kDefaultMicroIconY, dirty);

    const float iconSize = 42.0f * std::clamp(cfg.microIconScale, 0.5f, 2.5f);
    const ImVec2 targetPos(
        std::clamp(fracX * dispW, 0.0f, (std::max)(0.0f, dispW - iconSize)),
        std::clamp(fracY * dispH, 0.0f, (std::max)(0.0f, dispH - iconSize)));

    const bool applyPosNow = !menuOpen || forceResetMicroPos_ || (menuOpen && !prevMenuOpen_);
    forceResetMicroPos_ = false;
    ImGui::SetNextWindowPos(targetPos, applyPosNow ? ImGuiCond_Always : ImGuiCond_Once);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings;
    if (!menuOpen) {
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;
    } else {
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.89f, 0.96f, 0.40f));
    }

    if (ImGui::Begin("##SvPlusMicroIcon", nullptr, flags)) {
        const bool wasDrag = draggingMicro_;
        updateHudDragCursor(menuOpen, draggingMicro_);

        IDirect3DTexture9* tex = textures.microPassive;
        if (capture.isServerMuted() || !cfg.microEnable || !capture.hasMicrophone()) {
            tex = textures.microMuted;
        } else if (capture.isTransmitting()) {
            tex = textures.microActive;
        }

        if (tex) {
            drawIconWithMipFilter(tex, ImVec2(iconSize, iconSize), D3D9Hook::instance().device());
        } else {
            ImGui::TextColored(
                capture.isTransmitting() ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                "%s",
                capture.isTransmitting() ? "[MIC ON]" : "[MIC]");
        }

        if (menuOpen) {
            const ImVec2 currentPos = ImGui::GetWindowPos();
            if (std::fabs(currentPos.x - targetPos.x) > 1.0f || std::fabs(currentPos.y - targetPos.y) > 1.0f) {
                cfg.microIconX = std::clamp(currentPos.x / dispW, 0.0f, 1.0f);
                cfg.microIconY = std::clamp(currentPos.y / dispH, 0.0f, 1.0f);
                dirty = true;
            }
            if (wasDrag && !draggingMicro_) {
                SVPLUS_LOG_INF_CAT("ui", "HUD micro icon moved to (%.4f, %.4f)", cfg.microIconX, cfg.microIconY);
            }
        }
    }
    ImGui::End();
    if (menuOpen) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

void HudOverlay::renderSpeakerList(config::ClientConfig& cfg,
                                   const Localization& loc,
                                   const UiTextures& textures,
                                   const std::vector<audio::ActiveSpeakerInfo>& speakers,
                                   bool menuOpen,
                                   bool& dirty) {
    const auto& samp = samp::SampBridge::instance();
    const bool localTalking = audio::CaptureEngine::instance().isTransmitting();
    const std::uint16_t localId = samp.getLocalPlayerId();
    const std::string localName = localTalking ? samp.getLocalPlayerName() : std::string{};
    const std::uint32_t localColor = localTalking ? samp.getPlayerColorArgb(localId) : 0xFFFFFFFFu;
    const auto localStreams = localTalking ? net::VoiceClient::instance().localSpeakerStreamsSnapshot() : nullptr;
    const std::span<const net::LocalSpeakerStreamTag> localSpan = localStreams
        ? std::span<const net::LocalSpeakerStreamTag>(*localStreams)
        : std::span<const net::LocalSpeakerStreamTag>{};

    renderSpeakerListWithState(
        cfg,
        loc,
        textures,
        localTalking,
        localId,
        localName,
        localColor,
        localSpan,
        speakers,
        [&samp](std::uint16_t pid) { return samp.getPlayerColorArgb(pid); },
        menuOpen,
        dirty);
}

void HudOverlay::renderSpeakerListWithState(config::ClientConfig& cfg,
                                            const Localization& loc,
                                            const UiTextures& textures,
                                            bool localTalking,
                                            std::uint16_t localId,
                                            std::string_view localName,
                                            std::uint32_t localColorArgb,
                                            std::span<const net::LocalSpeakerStreamTag> localStreams,
                                            const std::vector<audio::ActiveSpeakerInfo>& speakers,
                                            const std::function<std::uint32_t(std::uint16_t)>& resolvePlayerColor,
                                            bool menuOpen,
                                            bool& dirty,
                                            const std::function<bool(std::uint32_t)>& isGlobalStreamJoined) {
    if (speakers.empty() && !localTalking && !menuOpen) {
        draggingSpeakerList_ = false;
        return;
    }

    const auto& io = ImGui::GetIO();
    const float dispW = (std::max)(1.0f, io.DisplaySize.x);
    const float dispH = (std::max)(1.0f, io.DisplaySize.y);
    const float fracX = config::resolveHudAxisFraction(cfg.speakerListX, dispW, config::kDefaultSpeakerListX, dirty);
    const float fracY = config::resolveHudAxisFraction(cfg.speakerListY, dispH, config::kDefaultSpeakerListY, dirty);
    const ImVec2 targetPos(
        std::clamp(fracX * dispW, 0.0f, (std::max)(0.0f, dispW - 40.0f)),
        std::clamp(fracY * dispH, 0.0f, (std::max)(0.0f, dispH - 24.0f)));

    const bool applyPosNow = !menuOpen || forceResetSpeakerListPos_ || (menuOpen && !prevMenuOpen_);
    forceResetSpeakerListPos_ = false;
    ImGui::SetNextWindowPos(targetPos, applyPosNow ? ImGuiCond_Always : ImGuiCond_Once);

    const int bgOpacity = cfg.speakerListBg ? std::clamp(cfg.speakerListBgOpacity, 0, 100) : 0;
    ImGui::SetNextWindowBgAlpha(static_cast<float>(bgOpacity) / 100.0f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings;
    if (!menuOpen) {
        flags |= ImGuiWindowFlags_NoInputs;
    }
    if (bgOpacity == 0) {
        flags |= ImGuiWindowFlags_NoBackground;
    }

    if (ImGui::Begin("##SvPlusSpeakerList", nullptr, flags)) {
        const bool wasDrag = draggingSpeakerList_;
        updateHudDragCursor(menuOpen, draggingSpeakerList_);

        const float scale = std::clamp(cfg.speakerListScale, 0.75f, 2.0f);
        IDirect3DDevice9* device = D3D9Hook::instance().device();
        ImGui::SetWindowFontScale(scale);

        const char* hdrText = loc.tr(TextKey::SpeakerListHeader);
        if (cfg.speakerListHeader) {
            ImGui::TextColored(ImVec4(1.0f, 224.0f / 255.0f, 138.0f / 255.0f, 1.0f),
                               "%s", hdrText);
        }

        if (localTalking) {
            const ImU32 localCol = argbToImU32FullAlpha(localColorArgb);

            std::string displayName(localName);
            if (displayName.empty()) {
                displayName = (localId != protocol::kInvalidPlayerId)
                    ? ("Player " + std::to_string(localId))
                    : "Player";
            }

            char prefix[256]{};
            if (localId != protocol::kInvalidPlayerId) {
                std::snprintf(prefix, sizeof(prefix), "[%u] %s",
                              static_cast<unsigned>(localId),
                              displayName.c_str());
            } else {
                std::snprintf(prefix, sizeof(prefix), "%s", displayName.c_str());
            }

            std::vector<ColoredSegment> segs;
            segs.push_back({prefix, localCol});
            for (const auto& tag : localStreams) {
                if (tag.name.empty() || tag.color == 0u) {
                    continue;
                }
                if (tag.kind == 0) {
                    const bool joined = isGlobalStreamJoined
                        ? isGlobalStreamJoined(tag.streamId)
                        : audio::PlaybackEngine::instance().hasStream(tag.streamId);
                    if (!joined) {
                        continue;
                    }
                }
                segs.push_back({" [", localCol});
                segs.push_back({tag.name, argbToImU32FullAlpha(tag.color)});
                segs.push_back({"]", localCol});
            }
            drawSpeakerListRow(textures.microActive, device, scale, segs);
        }

        const ImU32 blockedCol = argbToImU32FullAlpha(protocol::kBlockedAccentArgb);
        const std::size_t maxSpeakers = static_cast<std::size_t>(std::clamp(cfg.speakerListMax, 1, 15));
        const std::size_t limit = (std::min)(speakers.size(), maxSpeakers);
        for (std::size_t i = 0; i < limit; ++i) {
            const auto& sp = speakers[i];
            const ImU32 playerCol = argbToImU32FullAlpha(
                resolvePlayerColor ? resolvePlayerColor(sp.playerId) : 0xFFFFFFFFu);

            char prefix[256]{};
            std::snprintf(prefix, sizeof(prefix), "[%u] %s",
                          static_cast<unsigned>(sp.playerId),
                          sp.playerName.c_str());

            std::vector<ColoredSegment> segs;
            segs.push_back({prefix, playerCol});

            if (!sp.streams.empty()) {
                for (const auto& tag : sp.streams) {
                    if (tag.name.empty() || tag.color == 0u) {
                        continue;
                    }
                    segs.push_back({" [", playerCol});
                    segs.push_back({tag.name, argbToImU32FullAlpha(tag.color)});
                    segs.push_back({"]", playerCol});
                }
            } else if (!sp.streamName.empty() && sp.streamColor != 0u) {
                segs.push_back({" [", playerCol});
                segs.push_back({sp.streamName, argbToImU32FullAlpha(sp.streamColor)});
                segs.push_back({"]", playerCol});
            }

            if (sp.blocked) {
                segs.push_back({" [", playerCol});
                segs.push_back({"BLACKLIST", blockedCol});
                segs.push_back({"]", playerCol});
            }

            drawSpeakerListRow(textures.streamIconTexture(sp.iconId, sp.blocked), device, scale, segs);
        }

        // Empty draggable box when header OFF and nobody is speaking: exact WxH of SpeakerListHeader.
        if (menuOpen && !cfg.speakerListHeader && !localTalking && limit == 0) {
            const ImVec2 hdrSz = ImGui::CalcTextSize(hdrText);
            const float boxH = (std::max)(16.0f * scale, hdrSz.y);
            ImGui::Dummy(ImVec2(std::ceil(hdrSz.x), boxH));
            if (bgOpacity == 0) {
                if (ImDrawList* dl = ImGui::GetWindowDrawList()) {
                    const ImVec2 wMin  = ImGui::GetWindowPos();
                    const ImVec2 wSize = ImGui::GetWindowSize();
                    const ImVec2 wMax(wMin.x + wSize.x, wMin.y + wSize.y);
                    const float rounding = ImGui::GetStyle().WindowRounding;
                    dl->AddRect(
                        ImVec2(wMin.x + 0.5f, wMin.y + 0.5f),
                        ImVec2(wMax.x - 0.5f, wMax.y - 0.5f),
                        IM_COL32(20, 26, 36, 185),
                        rounding,
                        0,
                        1.0f);
                    dl->AddRect(
                        ImVec2(wMin.x + 1.5f, wMin.y + 1.5f),
                        ImVec2(wMax.x - 1.5f, wMax.y - 1.5f),
                        IM_COL32(225, 232, 242, 185),
                        (std::max)(0.0f, rounding - 1.0f),
                        0,
                        1.0f);
                }
            }
        }

        if (menuOpen) {
            const ImVec2 currentPos = ImGui::GetWindowPos();
            if (std::fabs(currentPos.x - targetPos.x) > 1.0f || std::fabs(currentPos.y - targetPos.y) > 1.0f) {
                cfg.speakerListX = std::clamp(currentPos.x / dispW, 0.0f, 1.0f);
                cfg.speakerListY = std::clamp(currentPos.y / dispH, 0.0f, 1.0f);
                dirty = true;
            }
            if (wasDrag && !draggingSpeakerList_) {
                SVPLUS_LOG_INF_CAT("ui", "HUD speaker list moved to (%.4f, %.4f)", cfg.speakerListX, cfg.speakerListY);
            }
        }
    }
    ImGui::End();
}

void HudOverlay::renderOverhead3DIcons(const config::ClientConfig& cfg,
                                       const UiTextures& textures,
                                       const std::vector<audio::ActiveSpeakerInfo>& speakers) {
    if ((!textures.speaker && !textures.speakerBlocked) || speakers.empty()) {
        return;
    }

    refreshSaMpFontSizeIfNeeded();

    const auto& samp = samp::SampBridge::instance();
    const auto& io   = ImGui::GetIO();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) {
        return;
    }

    const int screenW = tryReadRsGlobalScreenWidth(static_cast<int>(io.DisplaySize.x));
    const float hSamp = computeSampNametagHeight(screenW, cachedSaMpFontSize_);

    float camPos[3]{};
    const bool hasCam = samp.getCameraWorldPosition(camPos);

    IDirect3DDevice9* device = D3D9Hook::instance().device();
    bool mipEnabled = false;
    for (const auto& sp : speakers) {
        float worldPos[3]{};
        if (!samp.getRemotePlayerHeadBonePosition(sp.playerId, worldPos)) {
            continue;
        }

        float dist = 0.0f;
        if (hasCam) {
            const float dx = worldPos[0] - camPos[0];
            const float dy = worldPos[1] - camPos[1];
            const float dz = worldPos[2] - camPos[2];
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Apply exact SA-MP CPlayerTags Z offset before projecting to screen space.
        float tagWorld[3]{
            worldPos[0],
            worldPos[1],
            worldPos[2] + dist * 0.0475f + 0.2f
        };

        float screenPos[2]{};
        float projDist = 0.0f;
        if (!samp.worldToScreen(tagWorld, screenPos, &projDist)) {
            continue;
        }
        if (!hasCam) {
            dist = projDist;
        }
        if (dist > 60.0f) {
            continue;
        }

        IDirect3DTexture9* iconTex = textures.streamIconTexture(sp.iconId, sp.blocked);
        if (!iconTex) {
            continue;
        }

        if (!mipEnabled && device) {
            draw->AddCallback(&enableMipLinearCallback, device);
            mipEnabled = true;
        }

        const float distScale  = std::clamp(18.0f / (std::max)(4.0f, dist), 0.45f, 1.35f);
        const float halfSize   = 14.0f * cfg.speakerIconScale * distScale;
        const float iconSize   = 2.0f * halfSize;
        const int   bgOpacity  = std::clamp(cfg.overheadIconBgOpacity, 0, 100);
        const float outerHalf  = (bgOpacity > 0) ? (0.62f * iconSize) : halfSize;
        const ImVec2 center(
            screenPos[0],
            computeOverheadIconCenterY(screenPos[1], hSamp, outerHalf, cfg.overheadIconHeight, distScale));

        float iconHalf = halfSize;
        if (bgOpacity > 0) {
            const float discRadius  = outerHalf;
            const int   discAlpha   = (255 * bgOpacity + 50) / 100;
            const int   borderAlpha = (discAlpha * 40 + 50) / 100;
            iconHalf = 0.78f * discRadius;

            draw->AddCircleFilled(center, discRadius, IM_COL32(30, 30, 30, discAlpha), 24);
            draw->AddCircle(center, discRadius, IM_COL32(0, 0, 0, borderAlpha), 24, 1.0f);
        }

        draw->AddImage(
            reinterpret_cast<ImTextureID>(iconTex),
            ImVec2(center.x - iconHalf, center.y - iconHalf),
            ImVec2(center.x + iconHalf, center.y + iconHalf));
    }
    if (mipEnabled && device) {
        draw->AddCallback(&disableMipFilterCallback, device);
    }
}

} // namespace svplus::client::ui
