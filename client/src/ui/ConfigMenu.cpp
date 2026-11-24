#include "ui/ConfigMenu.hpp"

#include "audio/CaptureEngine.hpp"
#include "net/VoiceClient.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Version.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/HudOverlay.hpp"
#include "util/Utf8Util.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace svplus::client::ui {
namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

int compareIgnoreCase(std::string_view a, std::string_view b) noexcept {
    const std::size_t minLen = (std::min)(a.size(), b.size());
    for (std::size_t i = 0; i < minLen; ++i) {
        const unsigned char ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
        const unsigned char cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool sliderFloatWithArrows(const char* id,
                           float& value,
                           float minVal,
                           float maxVal,
                           float step,
                           const char* fmt,
                           float totalWidth,
                           const char* rightLabel = nullptr) {
    bool changed = false;
    const auto& style = ImGui::GetStyle();
    const float btnW = ImGui::GetFrameHeight();
    const float sliderW = (std::max)(60.0f, totalWidth - 2.0f * (btnW + style.ItemInnerSpacing.x));

    ImGui::PushID(id);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) {
        const float next = std::clamp(std::round((value - step) * 100.0f) / 100.0f, minVal, maxVal);
        if (std::fabs(next - value) > 1e-4f) {
            value = next;
            changed = true;
        }
    }
    ImGui::PopButtonRepeat();

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderFloat("##slider", &value, minVal, maxVal, fmt)) {
        value = std::clamp(value, minVal, maxVal);
        changed = true;
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) {
        const float next = std::clamp(std::round((value + step) * 100.0f) / 100.0f, minVal, maxVal);
        if (std::fabs(next - value) > 1e-4f) {
            value = next;
            changed = true;
        }
    }
    ImGui::PopButtonRepeat();

    if (rightLabel && rightLabel[0] != '\0') {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(rightLabel);
    }
    ImGui::PopID();
    return changed;
}

bool sliderIntWithArrows(const char* id,
                         int& value,
                         int minVal,
                         int maxVal,
                         int step,
                         const char* fmt,
                         float totalWidth,
                         const char* rightLabel = nullptr) {
    bool changed = false;
    const auto& style = ImGui::GetStyle();
    const float btnW = ImGui::GetFrameHeight();
    const float sliderW = (std::max)(60.0f, totalWidth - 2.0f * (btnW + style.ItemInnerSpacing.x));

    ImGui::PushID(id);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) {
        const int next = std::clamp(value - step, minVal, maxVal);
        if (next != value) {
            value = next;
            changed = true;
        }
    }
    ImGui::PopButtonRepeat();

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderInt("##slider", &value, minVal, maxVal, fmt)) {
        value = std::clamp(value, minVal, maxVal);
        changed = true;
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) {
        const int next = std::clamp(value + step, minVal, maxVal);
        if (next != value) {
            value = next;
            changed = true;
        }
    }
    ImGui::PopButtonRepeat();

    if (rightLabel && rightLabel[0] != '\0') {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(rightLabel);
    }
    ImGui::PopID();
    return changed;
}

struct BrandHeaderMetrics {
    ImVec2 size{0.0f, 28.0f};
    float  titleBaseline{0.0f};
};

BrandHeaderMetrics drawBrandHeader(ImDrawList* dl, ImVec2 origin, ImFont* titleFont, ImFont* badgeFont, bool talking) noexcept {
    constexpr float kBrandHeight = 28.0f;
    const ImU32 kColTop    = IM_COL32(0xFF, 0xE0, 0x8A, 255);
    const ImU32 kColBot    = IM_COL32(0xD4, 0xF0, 0x6A, 255);
    const ImU32 kColShadow = IM_COL32(0x00, 0x00, 0x00, 153);
    const ImU32 kColPillBg = IM_COL32(0xD4, 0xF0, 0x6A, 255);
    const ImU32 kColPillFg = IM_COL32(0x1E, 0x1E, 0x1E, 255);

    ImFont* bodyFont = ImGui::GetFont();
    const float bodySize = ImGui::GetFontSize();
    const float fallbackBaseline = origin.y + kBrandHeight * 0.5f +
        (bodyFont ? (bodyFont->Ascent * (bodySize / bodyFont->FontSize) * 0.5f) : (bodySize * 0.4f));

    if (!dl) {
        return {ImVec2(140.0f, kBrandHeight), fallbackBaseline};
    }

    char verText[32]{};
    std::snprintf(verText, sizeof(verText), "v%s", svplus::version::kString);
    ImFont* smallFont = badgeFont ? badgeFont : bodyFont;
    const float smallPx = badgeFont ? badgeFont->FontSize : std::max(11.5f, (bodyFont ? bodyFont->FontSize : 17.0f) * 0.72f);
    const ImU32 verCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    constexpr const char* kBrandName = "SampVoice";
    const float titlePx = titleFont ? titleFont->FontSize : (bodyFont ? bodyFont->FontSize : 17.0f);
    const ImVec2 titleSz = titleFont
        ? titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.0f, kBrandName)
        : ImGui::CalcTextSize("SampVoice Plus");
    const float brandH = std::max(kBrandHeight, titleSz.y);

    // 1. Equalizer sound bars on the left (centered inside brandH)
    float x = origin.x;
    const float baseHeights[3] = {12.0f, 22.0f, 16.0f};
    constexpr float barW = 4.5f;
    constexpr float barGap = 3.0f;
    const double t = talking ? ImGui::GetTime() : 0.0;

    for (int i = 0; i < 3; ++i) {
        float h = baseHeights[i];
        if (talking) {
            const float wave = 0.68f + 0.32f * std::sin(static_cast<float>(t) * (9.0f + i * 2.5f) + i * 1.4f);
            h = std::max(6.0f, h * wave);
        }
        const float yTop = origin.y + (brandH - h) * 0.5f;
        const float yBot = yTop + h;
        const ImU32 col = (i == 1) ? kColTop : IM_COL32(0xEC, 0xE8, 0x7A, 255);
        dl->AddRectFilled(ImVec2(x, yTop), ImVec2(x + barW, yBot), col, barW * 0.5f);
        x += barW + barGap;
    }
    x += 5.0f;

    if (!titleFont) {
        char fallback[96]{};
        std::snprintf(fallback, sizeof(fallback), "SampVoice Plus %s", verText);
        const ImVec2 sz = ImGui::CalcTextSize(fallback);
        const float fbY = origin.y + (brandH - sz.y) * 0.5f;
        const float fbBaseline = fbY + (bodyFont ? bodyFont->Ascent * (bodySize / bodyFont->FontSize) : bodySize * 0.8f);
        dl->AddText(ImVec2(x, fbY), kColTop, fallback);
        return {ImVec2((x - origin.x) + sz.x, std::max(brandH, sz.y)), fbBaseline};
    }

    // 2. "SampVoice" in Noto Sans Black with drop shadow + vertical gradient (never extends above origin.y)
    const float titleY = origin.y + (brandH - titleSz.y) * 0.5f;
    const ImVec2 titlePos(x, titleY);

    dl->AddText(titleFont, titlePx, ImVec2(titlePos.x + 1.5f, titlePos.y + 1.5f), kColShadow, kBrandName);
    const int vertStart = dl->VtxBuffer.Size;
    dl->AddText(titleFont, titlePx, titlePos, IM_COL32_WHITE, kBrandName);
    const int vertEnd = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        dl, vertStart, vertEnd,
        titlePos, ImVec2(titlePos.x, titlePos.y + titleSz.y),
        kColTop, kColBot);
    x += titleSz.x + 8.0f;

    // 3. "PLUS" pill badge using crisp 12 px badgeFont, baseline-aligned with "SampVoice"
    constexpr const char* kPillText = "PLUS";
    const ImVec2 pillTextSz = smallFont->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, kPillText);
    const float titleBaseline = titleY + titleFont->Ascent * (titlePx / titleFont->FontSize);
    const float smallAscent = smallFont->Ascent * (smallPx / smallFont->FontSize);

    const float pillPadX = 7.0f;
    const float pillPadY = 2.5f;
    const float pillW = pillTextSz.x + pillPadX * 2.0f;
    const float pillH = pillTextSz.y + pillPadY * 2.0f;
    const float pillTextY = titleBaseline - smallAscent;
    const float pillY = pillTextY - pillPadY;

    dl->AddRectFilled(ImVec2(x, pillY), ImVec2(x + pillW, pillY + pillH), kColPillBg, pillH * 0.5f);
    dl->AddText(smallFont, smallPx, ImVec2(x + pillPadX, pillTextY), kColPillFg, kPillText);
    x += pillW + 7.0f;

    // 4. Version tag ("v1.0.0") using crisp 12 px badgeFont, baseline-aligned
    const ImVec2 verSz = smallFont->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, verText);
    dl->AddText(smallFont, smallPx, ImVec2(x, titleBaseline - smallAscent), verCol, verText);
    x += verSz.x;

    return {ImVec2(x - origin.x, brandH), titleBaseline};
}

} // namespace

ConfigMenu& ConfigMenu::instance() noexcept {
    static auto* inst = new ConfigMenu();
    return *inst;
}

void ConfigMenu::setOpen(bool open) noexcept {
    if (open != open_) {
        SVPLUS_LOG_INF_CAT("ui", "Config menu %s", open ? "opened" : "closed");
    }
    if (open && !open_) {
        applyingOpeningLayout_ = true;
    }
    open_ = open;
    if (!open) {
        capturingHotkey_ = false;
    }
}

bool isNeverConsumedSystemMessage(UINT msg, WPARAM wParam) noexcept {
    switch (msg) {
        case WM_SYSCOMMAND:
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
        case WM_NCACTIVATE:
        case WM_KILLFOCUS:
        case WM_SETFOCUS:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
            return true;
        case WM_KEYDOWN:
        case WM_KEYUP:
            return (wParam == VK_LWIN || wParam == VK_RWIN);
        default:
            return false;
    }
}

bool isMenuUiInputMessage(UINT msg) noexcept {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return true;
        default:
            return false;
    }
}

void drawOverheadPreviewBox(ImTextureID speakerTex, int opacityPct, float uiAlpha) noexcept {
    const float boxSize = 32.0f;
    ImGui::Dummy(ImVec2(boxSize, boxSize));
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Light #EDEFF3 backdrop square so dark disc opacity contrast is genuinely visible.
    const int boxAlpha = static_cast<int>(std::lround(255.0f * uiAlpha));
    draw->AddRectFilled(pMin, pMax, IM_COL32(237, 239, 243, boxAlpha), 4.0f);
    draw->AddRect(pMin, pMax, IM_COL32(180, 186, 196, boxAlpha), 4.0f, 0, 1.0f);

    const float slotSize = 22.0f;
    float iconHalf = slotSize * 0.5f;
    const int bgOpacity = std::clamp(opacityPct, 0, 100);
    if (bgOpacity > 0) {
        const float discRadius  = 0.62f * slotSize;
        const int   discAlpha   = static_cast<int>(std::lround(((255 * bgOpacity + 50) / 100) * uiAlpha));
        const int   borderAlpha = static_cast<int>(std::lround((((255 * bgOpacity + 50) / 100) * 40 + 50) / 100 * uiAlpha));
        iconHalf = 0.78f * discRadius;
        draw->AddCircleFilled(center, discRadius, IM_COL32(30, 30, 30, discAlpha), 24);
        draw->AddCircle(center, discRadius, IM_COL32(0, 0, 0, borderAlpha), 24, 1.0f);
    }
    if (speakerTex) {
        const int iconAlpha = static_cast<int>(std::lround(255.0f * uiAlpha));
        draw->AddImage(
            speakerTex,
            ImVec2(center.x - iconHalf, center.y - iconHalf),
            ImVec2(center.x + iconHalf, center.y + iconHalf),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, iconAlpha));
    }
}

void drawRealSizeIconPreview(ImTextureID tex, float sizePx, float uiAlpha) noexcept {
    const float clamped = std::clamp(sizePx, 10.0f, 128.0f);
    ImGui::Dummy(ImVec2(clamped, clamped));
    if (!tex) {
        return;
    }
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const int alpha = static_cast<int>(std::lround(255.0f * uiAlpha));
    ImGui::GetWindowDrawList()->AddImage(
        tex, pMin, pMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha));
}

bool ConfigMenu::handleWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam,
                                     config::ClientConfig& cfg,
                                     bool chatOrGameMenuOpen,
                                     bool& outConfigDirty) {
    // R20-4: Never swallow Windows key, Alt+Tab, WM_SYSCOMMAND, or focus/activation transitions.
    if (isNeverConsumedSystemMessage(msg, wParam)) {
        const bool lostFocus =
            (msg == WM_KILLFOCUS) ||
            (msg == WM_ACTIVATEAPP && wParam == FALSE) ||
            (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE);
        if (lostFocus && open_) {
            open_ = false;
            capturingHotkey_ = false;
        }
        return false;
    }

    std::uint32_t pressedVk = 0;
    bool isKeyDownEvent = false;

    if (msg == WM_KEYDOWN) {
        if ((lParam & (1 << 30)) == 0) { // ignore key auto-repeat
            pressedVk = static_cast<std::uint32_t>(wParam);
            isKeyDownEvent = true;
        }
    } else if (capturingHotkey_) {
        if (msg == WM_MBUTTONDOWN) {
            pressedVk = VK_MBUTTON;
            isKeyDownEvent = true;
        } else if (msg == WM_XBUTTONDOWN) {
            pressedVk = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
            isKeyDownEvent = true;
        }
    }

    if (capturingHotkey_ && isKeyDownEvent && pressedVk != 0) {
        capturingHotkey_ = false;
        if (pressedVk != VK_ESCAPE) {
            cfg.menuHotkey = pressedVk;
            outConfigDirty = true;
        }
        return true;
    }

    if (isKeyDownEvent && pressedVk == cfg.menuHotkey && !chatOrGameMenuOpen) {
        setOpen(!open_);
        return true;
    }

    if (open_ && isKeyDownEvent && pressedVk == VK_ESCAPE) {
        setOpen(false);
        return true;
    }

    return open_ && isMenuUiInputMessage(msg);
}

void ConfigMenu::render(config::ClientConfig& cfg,
                        Localization& loc,
                        const UiTextures& textures,
                        bool& outConfigDirty) {
    if (!open_) {
        return;
    }

    const auto& io = ImGui::GetIO();
    const float dispW = (std::max)(1.0f, io.DisplaySize.x);
    const float dispH = (std::max)(1.0f, io.DisplaySize.y);
    const float uiScale = std::clamp(dispH / 1080.0f, 0.75f, 2.0f);
    const float defaultW = (std::min)(dispW * 0.95f, 1002.0f * uiScale);
    const float defaultH = (std::min)(dispH * 0.95f, 666.0f * uiScale);

    const float targetW = (cfg.menuW > 0.0f)
        ? std::clamp(cfg.menuW * dispW, 560.0f, dispW * 0.98f)
        : defaultW;
    const float targetH = (cfg.menuH > 0.0f)
        ? std::clamp(cfg.menuH * dispH, 380.0f, dispH * 0.98f)
        : defaultH;
    const float targetX = (cfg.menuX >= 0.0f)
        ? std::clamp(cfg.menuX * dispW, 0.0f, (std::max)(0.0f, dispW - 80.0f))
        : (dispW - targetW) * 0.5f;
    const float targetY = (cfg.menuY >= 0.0f)
        ? std::clamp(cfg.menuY * dispH, 0.0f, (std::max)(0.0f, dispH - 60.0f))
        : (dispH - targetH) * 0.5f;

    const ImGuiCond geomCond = applyingOpeningLayout_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(ImVec2(targetW, targetH), geomCond);
    ImGui::SetNextWindowPos(ImVec2(targetX, targetY), geomCond);
    applyingOpeningLayout_ = false;

    bool keepOpen = open_;
    bool dirty    = false;

    char titleBuf[256]{};
    std::snprintf(titleBuf, sizeof(titleBuf), "%s###SvPlusConfig", loc.tr(TextKey::WindowTitle));

    const ImGuiWindowFlags winFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin(titleBuf, &keepOpen, winFlags)) {
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const float expectedW = (cfg.menuW > 0.0f) ? cfg.menuW * dispW : targetW;
        const float expectedH = (cfg.menuH > 0.0f) ? cfg.menuH * dispH : targetH;
        const float expectedX = (cfg.menuX >= 0.0f) ? cfg.menuX * dispW : targetX;
        const float expectedY = (cfg.menuY >= 0.0f) ? cfg.menuY * dispH : targetY;
        if (std::fabs(winPos.x - expectedX) > 1.5f || std::fabs(winPos.y - expectedY) > 1.5f ||
            std::fabs(winSize.x - expectedW) > 1.5f || std::fabs(winSize.y - expectedH) > 1.5f) {
            cfg.menuX = std::clamp(winPos.x / dispW, 0.0f, 1.0f);
            cfg.menuY = std::clamp(winPos.y / dispH, 0.0f, 1.0f);
            cfg.menuW = std::clamp(winSize.x / dispW, 0.1f, 1.0f);
            cfg.menuH = std::clamp(winSize.y / dispH, 0.1f, 1.0f);
            dirty = true;
        }

        // Stylized brand header & vertically aligned SA-MP version/protocol label
        const ImVec2 brandOrigin = ImGui::GetCursorScreenPos();
        const bool isTalking = audio::CaptureEngine::instance().isTransmitting();
        ImFont* titleFnt = D3D9Hook::instance().titleFont();
        ImFont* badgeFnt = D3D9Hook::instance().badgeFont();
        if (!titleFnt && io.Fonts && io.Fonts->Fonts.Size >= 3) {
            titleFnt = io.Fonts->Fonts[1];
            badgeFnt = io.Fonts->Fonts[2];
        }
        const BrandHeaderMetrics brand = drawBrandHeader(
            ImGui::GetWindowDrawList(),
            brandOrigin,
            titleFnt,
            badgeFnt,
            isTalking);
        ImGui::Dummy(brand.size);
        ImGui::SameLine(0.0f, 18.0f);

        const auto& voiceClient = net::VoiceClient::instance();
        const auto& samp        = samp::SampBridge::instance();
        const bool connected    = voiceClient.isVoiceConnected();

        ImFont* bodyFnt = ImGui::GetFont();
        const float bodyPx = ImGui::GetFontSize();
        const float bodyAscent = bodyFnt
            ? (bodyFnt->Ascent * (bodyPx / bodyFnt->FontSize))
            : (bodyPx * 0.8f);
        const float groupTopY = ImGui::GetCursorScreenPos().y;
        const float textVertOffset = (std::max)(0.0f, brand.titleBaseline - bodyAscent - groupTopY);
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textVertOffset);
        if (connected && voiceClient.rttMs() >= 0.0f) {
            ImGui::TextDisabled("%s: %s (%s)  |  %s: %.0f ms",
                                loc.tr(TextKey::StatusSampVersion),
                                samp.versionName(),
                                voiceClient.isServerPlus() ? "v12" : "v11",
                                loc.tr(TextKey::StatusRttMs),
                                voiceClient.rttMs());
        } else {
            ImGui::TextDisabled("%s: %s (%s)",
                                loc.tr(TextKey::StatusSampVersion),
                                samp.versionName(),
                                voiceClient.isServerPlus() ? "v12" : "v11");
        }
        ImGui::EndGroup();

        ImGui::Separator();

        // R20-6: Status: <colored diagnostic state>
        {
            using VDS = net::VoiceDiagnosticState;
            const VDS diag = voiceClient.diagnosticState();
            ImVec4 diagCol(0.75f, 0.78f, 0.84f, 1.0f);
            char diagBuf[256]{};
            switch (diag) {
                case VDS::ConnectedOk:
                    diagCol = ImVec4(0.32f, 0.90f, 0.55f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagConnectedOk));
                    break;
                case VDS::ConnectedMicDisabled:
                    diagCol = ImVec4(0.96f, 0.80f, 0.28f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagConnectedMicDisabled));
                    break;
                case VDS::ConnectedNoMic:
                    diagCol = ImVec4(0.96f, 0.80f, 0.28f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagConnectedNoMic));
                    break;
                case VDS::ConnectedMuted:
                    diagCol = ImVec4(0.96f, 0.80f, 0.28f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagConnectedMuted));
                    break;
                case VDS::ListenerDisabled:
                    diagCol = ImVec4(0.96f, 0.80f, 0.28f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagListenerDisabled));
                    break;
                case VDS::Connecting:
                    diagCol = ImVec4(0.75f, 0.78f, 0.84f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagConnecting));
                    break;
                case VDS::UdpBlocked:
                    diagCol = ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), loc.tr(TextKey::DiagUdpBlocked),
                                  static_cast<unsigned>(voiceClient.serverUdpPort()));
                    break;
                case VDS::NoVoiceServer:
                    diagCol = ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagNoVoiceServer));
                    break;
                case VDS::Idle:
                default:
                    diagCol = ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
                    std::snprintf(diagBuf, sizeof(diagBuf), "%s", loc.tr(TextKey::DiagIdle));
                    break;
            }
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", loc.tr(TextKey::StatusLabel));
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(diagCol, "%s", diagBuf);
        }

        // R10-9: Prominent warning if SvAddKey matches menuHotkey
        if (audio::CaptureEngine::instance().hasKeyConflict()) {
            const std::string vkName = config::formatVirtualKeyName(
                audio::CaptureEngine::instance().conflictingKey());
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
                loc.tr(TextKey::HotkeyConflictWarning),
                vkName.c_str());
        }

        if (ImGui::BeginTabBar("##SvPlusTabs")) {
            auto switchTab = [&](int idx, const char* tabLabel) {
                if (activeTab_ != idx) {
                    activeTab_ = idx;
                    SVPLUS_LOG_INF_CAT("ui", "Switched config tab -> %s", tabLabel);
                }
            };
            char tabBuf[128]{};
            std::snprintf(tabBuf, sizeof(tabBuf), "%s###TabGeneral", loc.tr(TextKey::TabGeneral));
            if (ImGui::BeginTabItem(tabBuf)) {
                switchTab(0, "General");
                renderGeneralTab(cfg, loc, textures, dirty);
                ImGui::EndTabItem();
            }
            std::snprintf(tabBuf, sizeof(tabBuf), "%s###TabAudio", loc.tr(TextKey::TabAudio));
            if (ImGui::BeginTabItem(tabBuf)) {
                switchTab(1, "Audio");
                renderAudioTab(cfg, loc, dirty);
                ImGui::EndTabItem();
            }
            std::snprintf(tabBuf, sizeof(tabBuf), "%s###TabMicrophone", loc.tr(TextKey::TabMicrophone));
            if (ImGui::BeginTabItem(tabBuf)) {
                switchTab(2, "Microphone");
                renderMicrophoneTab(cfg, loc, dirty);
                ImGui::EndTabItem();
            }
            std::snprintf(tabBuf, sizeof(tabBuf), "%s###TabBlacklist", loc.tr(TextKey::TabBlacklist));
            if (ImGui::BeginTabItem(tabBuf)) {
                switchTab(3, "Blacklist");
                renderBlacklistTab(cfg, loc, dirty);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::TextDisabled("%s", loc.tr(TextKey::FooterHint));
    }
    ImGui::End();

    if (!keepOpen) {
        setOpen(false);
    }

    if (dirty) {
        outConfigDirty = true;
    }
}

void ConfigMenu::renderGeneralTab(config::ClientConfig& cfg, Localization& loc, const UiTextures& textures, bool& dirty) {
    const float uiAlpha = ImGui::GetStyle().Alpha;

    if (!ImGui::BeginTable("##SvPlusGeneralCols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        return;
    }
    ImGui::TableSetupColumn("##ColLeft", ImGuiTableColumnFlags_WidthStretch, 0.50f);
    ImGui::TableSetupColumn("##ColRight", ImGuiTableColumnFlags_WidthStretch, 0.50f);
    ImGui::TableNextRow();

    // -------------------------------------------------------------------------
    // Left Column: Language, Hotkey, Micro HUD, Overhead 3D Icons
    // -------------------------------------------------------------------------
    ImGui::TableSetColumnIndex(0);
    {
        // Language selector
        const auto langs = Localization::languages();
        const char* currentNative = Localization::nativeNameOf(loc.activeLanguage());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", loc.tr(TextKey::LanguageLabel));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(205.0f);
        if (ImGui::BeginCombo("##LangCombo", currentNative)) {
            for (const auto& info : langs) {
                const bool selected = (info.id == loc.activeLanguage());
                if (ImGui::Selectable(info.nativeName, selected)) {
                    loc.setActiveLanguage(info.id);
                    cfg.language = info.code;
                    dirty = true;
                    SVPLUS_LOG_INF_CAT("ui", "Selected language '%s' (%s)", info.code, info.nativeName);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // Menu Hotkey
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", loc.tr(TextKey::HotkeyLabel));
        ImGui::SameLine();
        const std::string keyLabel = capturingHotkey_
            ? std::string(loc.tr(TextKey::HotkeyPressAny))
            : config::formatVirtualKeyName(cfg.menuHotkey);
        if (ImGui::Button(keyLabel.c_str(), ImVec2(140.0f, 0.0f))) {
            capturingHotkey_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(loc.tr(TextKey::HotkeyResetDefault))) {
            capturingHotkey_ = false;
            if (cfg.menuHotkey != config::kDefaultMenuHotkey) {
                cfg.menuHotkey = config::kDefaultMenuHotkey;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset Hotkey -> default 0x%02X",
                               static_cast<unsigned>(config::kDefaultMenuHotkey));
        }

        ImGui::Separator();

        // Microphone HUD
        if (ImGui::Checkbox(loc.tr(TextKey::ShowMicroIcon), &cfg.showMicroIcon)) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showMicroIcon);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(loc.tr(TextKey::MicroIconScale));
        if (sliderFloatWithArrows("##MicroIconScaleSlider", cfg.microIconScale, 0.5f, 2.5f, 0.01f, "%.2fx", 175.0f)) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetMicroScale").c_str())) {
            if (cfg.microIconScale != config::kDefaultMicroIconScale) {
                cfg.microIconScale = config::kDefaultMicroIconScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset MicroIconScale -> %.2f", cfg.microIconScale);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 12.0f);
        drawRealSizeIconPreview(
            reinterpret_cast<ImTextureID>(textures.microActive),
            42.0f * std::clamp(cfg.microIconScale, 0.5f, 2.5f),
            uiAlpha);
        ImGui::EndDisabled();

        if (ImGui::Button(loc.tr(TextKey::ResetMicroPosition))) {
            cfg.microIconX = config::kDefaultMicroIconX;
            cfg.microIconY = config::kDefaultMicroIconY;
            HudOverlay::instance().requestResetMicroPosition();
            dirty = true;
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset MicroIconPosition");
        }

        ImGui::Separator();

        // Overhead 3D Icons
        if (ImGui::Checkbox(loc.tr(TextKey::ShowOverheadIcons), &cfg.showOverheadIcons)) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showOverheadIcons);

        ImGui::BeginGroup();
        ImGui::TextUnformatted(loc.tr(TextKey::OverheadIconBgOpacity));
        int opacityVal = std::clamp(cfg.overheadIconBgOpacity, 0, 100);
        if (sliderIntWithArrows("##OverheadOpacitySlider", opacityVal, 0, 100, 1, "%d%%", 175.0f)) {
            if (opacityVal != cfg.overheadIconBgOpacity) {
                cfg.overheadIconBgOpacity = opacityVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetOverheadOpacity").c_str())) {
            if (cfg.overheadIconBgOpacity != config::kDefaultOverheadIconBgOpacity) {
                cfg.overheadIconBgOpacity = config::kDefaultOverheadIconBgOpacity;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset OverheadIconBgOpacity -> %d%%", cfg.overheadIconBgOpacity);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 12.0f);
        drawOverheadPreviewBox(
            reinterpret_cast<ImTextureID>(textures.speaker),
            cfg.overheadIconBgOpacity,
            uiAlpha);

        ImGui::BeginGroup();
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerIconScale));
        if (sliderFloatWithArrows("##SpeakerIconScaleSlider", cfg.speakerIconScale, 0.5f, 2.5f, 0.01f, "%.2fx", 175.0f)) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetSpeakerIconScale").c_str())) {
            if (cfg.speakerIconScale != config::kDefaultSpeakerIconScale) {
                cfg.speakerIconScale = config::kDefaultSpeakerIconScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerIconScale -> %.2f", cfg.speakerIconScale);
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 12.0f);
        drawRealSizeIconPreview(
            reinterpret_cast<ImTextureID>(textures.speaker),
            28.0f * std::clamp(cfg.speakerIconScale, 0.5f, 2.5f),
            uiAlpha);

        ImGui::TextUnformatted(loc.tr(TextKey::OverheadIconHeight));
        int heightVal = std::clamp(cfg.overheadIconHeight, 0, 60);
        if (sliderIntWithArrows("##OverheadHeightSlider", heightVal, 0, 60, 1, "%d px", 175.0f)) {
            if (heightVal != cfg.overheadIconHeight) {
                cfg.overheadIconHeight = heightVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetOverheadHeight").c_str())) {
            if (cfg.overheadIconHeight != config::kDefaultOverheadIconHeight) {
                cfg.overheadIconHeight = config::kDefaultOverheadIconHeight;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset OverheadIconHeight -> %d px", cfg.overheadIconHeight);
        }

        ImGui::EndDisabled();
    }

    // -------------------------------------------------------------------------
    // Right Column: Speaker List HUD Controls & Preview
    // -------------------------------------------------------------------------
    ImGui::TableSetColumnIndex(1);
    {
        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerList), &cfg.showSpeakerList)) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showSpeakerList);

        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerListHeader), &cfg.speakerListHeader)) {
            dirty = true;
        }
        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerListBg), &cfg.speakerListBg)) {
            dirty = true;
        }

        ImGui::BeginDisabled(!cfg.speakerListBg);
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerListBgOpacity));
        int bgOpacityVal = std::clamp(cfg.speakerListBgOpacity, 0, 100);
        if (sliderIntWithArrows("##ListBgOpacitySlider", bgOpacityVal, 0, 100, 1, "%d%%", 205.0f)) {
            if (bgOpacityVal != cfg.speakerListBgOpacity) {
                cfg.speakerListBgOpacity = bgOpacityVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListBgOpacity").c_str())) {
            if (cfg.speakerListBgOpacity != config::kDefaultSpeakerListBgOpacity) {
                cfg.speakerListBgOpacity = config::kDefaultSpeakerListBgOpacity;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListBgOpacity -> %d%%", cfg.speakerListBgOpacity);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerListScale));
        if (sliderFloatWithArrows("##ListScaleSlider", cfg.speakerListScale, 0.75f, 2.0f, 0.01f, "%.2fx", 205.0f)) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListScale").c_str())) {
            if (cfg.speakerListScale != config::kDefaultSpeakerListScale) {
                cfg.speakerListScale = config::kDefaultSpeakerListScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListScale -> %.2f", cfg.speakerListScale);
        }
        ImGui::EndGroup();

        // Live scaled speaker-row preview to the right of SpeakerListScale
        ImGui::SameLine(0.0f, 12.0f);
        {
            const float sc = std::clamp(cfg.speakerListScale, 0.75f, 2.0f);
            const float iconSz = 16.0f * sc;
            ImFont* fnt = ImGui::GetFont();
            const float fontSz = (fnt ? fnt->FontSize : 17.0f) * sc;
            const char* sampleName = "Player [0]";
            const ImVec2 txtSz = fnt
                ? fnt->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, sampleName)
                : ImVec2(72.0f * sc, 17.0f * sc);
            const float rowW = iconSz + 6.0f + txtSz.x + 12.0f;
            const float rowH = (std::max)(iconSz, txtSz.y) + 8.0f;
            ImGui::Dummy(ImVec2(rowW, rowH));
            const ImVec2 pMin = ImGui::GetItemRectMin();
            const ImVec2 pMax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int bgA = cfg.speakerListBg
                ? static_cast<int>(std::lround((255.0f * cfg.speakerListBgOpacity / 100.0f) * uiAlpha))
                : 0;
            if (bgA > 0) {
                dl->AddRectFilled(pMin, pMax, IM_COL32(18, 22, 29, bgA), 4.0f);
            }
            const float iy = (pMin.y + pMax.y - iconSz) * 0.5f;
            if (textures.speaker) {
                dl->AddImage(
                    reinterpret_cast<ImTextureID>(textures.speaker),
                    ImVec2(pMin.x + 5.0f, iy),
                    ImVec2(pMin.x + 5.0f + iconSz, iy + iconSz),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                    IM_COL32(255, 255, 255, static_cast<int>(255 * uiAlpha)));
            }
            const float ty = (pMin.y + pMax.y - txtSz.y) * 0.5f;
            const float tx = pMin.x + 5.0f + iconSz + 5.0f;
            const float o = (std::max)(1.0f, 1.0f * sc);
            const int fgA = static_cast<int>(255 * uiAlpha);
            const ImU32 outCol = IM_COL32(0, 0, 0, fgA);
            constexpr float kDirs[8][2] = {
                {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
                {-1.0f,  0.0f},                {1.0f,  0.0f},
                {-1.0f,  1.0f}, {0.0f,  1.0f}, {1.0f,  1.0f},
            };
            for (const auto& d : kDirs) {
                dl->AddText(fnt, fontSz, ImVec2(tx + d[0] * o, ty + d[1] * o), outCol, sampleName);
            }
            dl->AddText(fnt, fontSz, ImVec2(tx, ty), IM_COL32(255, 255, 255, fgA), sampleName);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerListMax));
        int maxVal = std::clamp(cfg.speakerListMax, 1, 15);
        if (sliderIntWithArrows("##ListMaxSlider", maxVal, 1, 15, 1, "%d", 205.0f)) {
            cfg.speakerListMax = std::clamp(maxVal, 1, 15);
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListMax").c_str())) {
            if (cfg.speakerListMax != config::kDefaultSpeakerListMax) {
                cfg.speakerListMax = config::kDefaultSpeakerListMax;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListMax -> %d", cfg.speakerListMax);
        }

        ImGui::Spacing();
        if (ImGui::Button(loc.tr(TextKey::ResetSpeakerListPosition))) {
            cfg.speakerListX = config::kDefaultSpeakerListX;
            cfg.speakerListY = config::kDefaultSpeakerListY;
            HudOverlay::instance().requestResetSpeakerListPosition();
            dirty = true;
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListPosition");
        }

        ImGui::EndDisabled();
    }

    ImGui::EndTable();
}

void ConfigMenu::renderAudioTab(config::ClientConfig& cfg, Localization& loc, bool& dirty) {
    ImGui::Spacing();
    if (ImGui::Checkbox(loc.tr(TextKey::SoundEnable), &cfg.soundEnable)) {
        dirty = true;
    }
    if (sliderIntWithArrows("##SoundVolume", cfg.soundVolume, 0, 100, 1, "%d%%", 280.0f, loc.tr(TextKey::SoundVolume))) {
        dirty = true;
    }
    ImGui::Spacing();
    if (ImGui::Checkbox(loc.tr(TextKey::SoundBalancer), &cfg.soundBalancer)) {
        dirty = true;
    }
    if (ImGui::Checkbox(loc.tr(TextKey::SoundFilter), &cfg.soundFilter)) {
        dirty = true;
    }
}

void ConfigMenu::renderMicrophoneTab(config::ClientConfig& cfg, Localization& loc, bool& dirty) {
    ImGui::Spacing();
    auto& capture = audio::CaptureEngine::instance();

    if (ImGui::Checkbox(loc.tr(TextKey::MicroEnable), &cfg.microEnable)) {
        dirty = true;
    }

    // R10-9: Push-to-Talk vs Toggle talk mode selector
    const char* talkModeLabels[2] = {
        loc.tr(TextKey::TalkModePush),
        loc.tr(TextKey::TalkModeToggle)
    };
    int modeIdx = (cfg.talkMode == config::TalkMode::Toggle) ? 1 : 0;
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::Combo(loc.tr(TextKey::TalkModeLabel), &modeIdx, talkModeLabels, 2)) {
        cfg.talkMode = (modeIdx == 1) ? config::TalkMode::Toggle : config::TalkMode::PushToTalk;
        dirty = true;
    }

    // R10-7: Refresh capture devices periodically (~1.5s) while Microphone tab is open
    static double s_lastUiDevRefresh = 0.0;
    const double uiNow = ImGui::GetTime();
    if (uiNow - s_lastUiDevRefresh >= 1.5) {
        s_lastUiDevRefresh = uiNow;
        capture.requestRefreshDevices();
    }

    const auto devices = capture.devicesSnapshot();
    std::string currentDevName = cfg.microDevice;
    if (currentDevName.empty()) {
        currentDevName = loc.tr(TextKey::MicroDeviceDefault);
    }

    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo(loc.tr(TextKey::MicroDevice), currentDevName.c_str())) {
        if (ImGui::Selectable(loc.tr(TextKey::MicroDeviceDefault), cfg.microDevice.empty())) {
            cfg.microDevice.clear();
            capture.requestSelectDevice("");
            dirty = true;
        }
        for (const auto& d : devices) {
            const bool selected = (cfg.microDevice == d.name);
            if (ImGui::Selectable(d.name.c_str(), selected)) {
                cfg.microDevice = d.name;
                capture.requestSelectDevice(d.name);
                dirty = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(loc.tr(TextKey::MicroResetDevice))) {
        capture.requestRefreshDevices();
        capture.requestSelectDevice(cfg.microDevice);
    }

    if (sliderIntWithArrows("##MicroVolume", cfg.microVolume, 0, 100, 1, "%d%%", 300.0f, loc.tr(TextKey::MicroVolume))) {
        dirty = true;
    }
    if (ImGui::Checkbox(loc.tr(TextKey::MicroNoiseSuppress), &cfg.microNoiseSuppress)) {
        dirty = true;
    }

    ImGui::Spacing();
    ImGui::Text("%s:", loc.tr(TextKey::MicroTestLevel));
    ImGui::SetNextItemWidth(300.0f);
    ImGui::ProgressBar(capture.currentInputLevel(), ImVec2(300.0f, 18.0f));
}

void ConfigMenu::renderBlacklistTabWithState(config::ClientConfig& cfg,
                                             Localization& loc,
                                             const BlacklistTabView& view,
                                             bool& dirty) {
    const auto& style = ImGui::GetStyle();
    // Exact height of the shared footer drawn below BeginTabBar (ItemSpacing + Separator + ItemSpacing + FooterHint + ItemSpacing).
    const float bottomReserve = ImGui::GetTextLineHeight() + style.ItemSpacing.y * 3.0f + 4.0f;
    const float availY = (std::max)(220.0f, ImGui::GetContentRegionAvail().y - bottomReserve);
    const float rowStartX = ImGui::GetCursorPosX();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float btnPadX = style.FramePadding.x * 2.0f;

    auto calcBtnWidth = [&](const char* label) {
        return ImGui::CalcTextSize(label).x + btnPadX;
    };

    // =========================================================================
    // Part 1: Current server (only active when connected; compact notice when disconnected)
    // =========================================================================
    if (!view.connected) {
        ImGui::TextColored(ImVec4(0.86f, 0.90f, 0.96f, 1.0f), "%s", loc.tr(TextKey::BlacklistCurrentServerHeader));
        ImGui::BeginChild("##Part1Disconnected", ImVec2(0.0f, 46.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistDisconnected));
        ImGui::EndChild();
    } else {
        std::string srvTitle = loc.tr(TextKey::BlacklistCurrentServerHeader);
        if (!view.serverName.empty() && !view.serverEndpoint.empty()) {
            srvTitle += ": " + view.serverName + " (" + view.serverEndpoint + ")";
        } else if (!view.serverEndpoint.empty()) {
            srvTitle += ": " + view.serverEndpoint;
        }

        const float searchW = (std::min)(260.0f, availW * 0.34f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.86f, 0.90f, 0.96f, 1.0f), "%.*s",
                           static_cast<int>(util::utf8TruncateLen(srvTitle, 92u)), srvTitle.c_str());
        ImGui::SameLine(rowStartX + (std::max)(180.0f, availW - searchW));
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##BlacklistSearch", loc.tr(TextKey::BlacklistSearchPlaceholder),
                                 searchBuf_, sizeof(searchBuf_));

        const float part1H = std::clamp(availY * 0.43f, 128.0f, 178.0f);
        const float leftW  = (std::max)(260.0f, (availW - style.ItemSpacing.x) * 0.57f);
        const float rightW = (std::max)(200.0f, availW - leftW - style.ItemSpacing.x);
        const std::string_view filter(searchBuf_);

        const char* muteHereTxt   = loc.tr(TextKey::BlacklistMuteHereBtn);
        const char* muteAllTxt    = loc.tr(TextKey::BlacklistMuteAllBtn);
        const char* unmuteTxt     = loc.tr(TextKey::BlacklistUnmuteButton);
        const char* unmuteHereTxt = loc.tr(TextKey::BlacklistUnmuteHereButton);
        const float pairBtnW      = calcBtnWidth(muteHereTxt) + calcBtnWidth(muteAllTxt) + style.ItemInnerSpacing.x;

        // Left child: Online players on current server
        ImGui::BeginChild("##OnlinePlayers", ImVec2(leftW, part1H), true);
        ImGui::TextUnformatted(loc.tr(TextKey::BlacklistPlayersOnline));
        ImGui::Separator();

        for (const auto& p : view.onlinePlayers) {
            if (p.name.empty()) {
                continue;
            }
            char rowLabel[96];
            std::snprintf(rowLabel, sizeof(rowLabel), "%s [%u]", p.name.c_str(), static_cast<unsigned>(p.id));
            if (!containsIgnoreCase(rowLabel, filter)) {
                continue;
            }

            const config::BlacklistEntry* blocking = cfg.findBlockingEntry(p.name, view.serverEndpoint);
            const config::BlacklistEntry* originHere = cfg.findOriginEntry(p.name, view.serverEndpoint);

            ImGui::PushID(static_cast<int>(p.id));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(rowLabel);

            const float contentAvail = ImGui::GetWindowContentRegionMax().x;
            if (!blocking) {
                ImGui::SameLine((std::max)(110.0f, contentAvail - pairBtnW));
                if (ImGui::SmallButton(muteHereTxt)) {
                    if (cfg.mutePlayer(p.name, view.serverName, view.serverEndpoint, config::BlacklistScope::Server)) {
                        dirty = true;
                    }
                }
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (ImGui::SmallButton(muteAllTxt)) {
                    if (cfg.mutePlayer(p.name, view.serverName, view.serverEndpoint, config::BlacklistScope::All)) {
                        dirty = true;
                    }
                }
            } else if (originHere) {
                const float uw = calcBtnWidth(unmuteTxt);
                ImGui::SameLine((std::max)(110.0f, contentAvail - uw));
                if (ImGui::SmallButton(unmuteTxt)) {
                    if (cfg.removeBlacklistEntry(p.name, view.serverEndpoint)) {
                        dirty = true;
                    }
                }
            } else {
                const float uhw = calcBtnWidth(unmuteHereTxt);
                ImGui::SameLine((std::max)(110.0f, contentAvail - uhw));
                if (ImGui::SmallButton(unmuteHereTxt)) {
                    if (cfg.setBlacklistEntryScope(blocking->playerName, blocking->serverEndpoint,
                                                   config::BlacklistScope::Server)) {
                        dirty = true;
                    }
                }
                const std::string originLabel =
                    !blocking->serverName.empty() ? blocking->serverName : blocking->serverEndpoint;
                ImGui::Indent(12.0f);
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f),
                                   loc.tr(TextKey::BlacklistMutedAllFromFmt), originLabel.c_str());
                ImGui::Unindent(12.0f);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right child: Blacklist entries active on the current server
        std::vector<const config::BlacklistEntry*> activeEntries;
        std::size_t clearableCount = 0;
        for (const auto& e : cfg.blacklistEntries) {
            const bool isLocal = equalsIgnoreCase(e.serverEndpoint, view.serverEndpoint);
            if (isLocal || e.scope == config::BlacklistScope::All) {
                activeEntries.push_back(&e);
                if (isLocal) {
                    ++clearableCount;
                }
            }
        }

        ImGui::BeginChild("##ActiveMuted", ImVec2(rightW, part1H), true);
        ImGui::Text("%s (%zu)", loc.tr(TextKey::BlacklistMutedList), activeEntries.size());
        if (clearableCount > 0) {
            const char* clrTxt = loc.tr(TextKey::BlacklistClearAll);
            ImGui::SameLine((std::max)(130.0f, ImGui::GetWindowContentRegionMax().x - calcBtnWidth(clrTxt)));
            if (ImGui::SmallButton(clrTxt)) {
                if (cfg.clearOriginServerBlacklist(view.serverEndpoint) > 0) {
                    dirty = true;
                }
            }
        }
        ImGui::Separator();

        if (activeEntries.empty()) {
            ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistEmpty));
        } else {
            const char* rmTxt = loc.tr(TextKey::BlacklistRemoveBtn);
            const float rmW   = calcBtnWidth(rmTxt);
            for (std::size_t idx = 0; idx < activeEntries.size(); ++idx) {
                const auto* e = activeEntries[idx];
                if (!containsIgnoreCase(e->playerName, filter) &&
                    !containsIgnoreCase(e->serverName, filter) &&
                    !containsIgnoreCase(e->serverEndpoint, filter)) {
                    continue;
                }
                const bool isAll = (e->scope == config::BlacklistScope::All);
                const char* scopeTag = isAll ? loc.tr(TextKey::BlacklistScopeAllLabel)
                                             : loc.tr(TextKey::BlacklistScopeServerLabel);
                const std::string pCopy  = e->playerName;
                const std::string epCopy = e->serverEndpoint;

                ImGui::PushID(static_cast<int>(2000 + idx));
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", pCopy.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", scopeTag);
                ImGui::SameLine((std::max)(110.0f, ImGui::GetWindowContentRegionMax().x - rmW));
                if (ImGui::SmallButton(rmTxt)) {
                    if (cfg.removeBlacklistEntry(pCopy, epCopy)) {
                        dirty = true;
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    // =========================================================================
    // Part 2: Full blacklist across all servers (always visible)
    // =========================================================================
    ImGui::Spacing();
    const float allSearchW = (std::min)(300.0f, availW * 0.38f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.86f, 0.90f, 0.96f, 1.0f), "%s (%zu)",
                       loc.tr(TextKey::BlacklistAllHeader), cfg.blacklistEntries.size());
    ImGui::SameLine(rowStartX + (std::max)(200.0f, availW - allSearchW));
    ImGui::SetNextItemWidth(allSearchW);
    ImGui::InputTextWithHint("##BlacklistAllSearch", loc.tr(TextKey::BlacklistAllSearchPlaceholder),
                             allSearchBuf_, sizeof(allSearchBuf_));

    const float part2H = (std::max)(110.0f, ImGui::GetContentRegionAvail().y - bottomReserve);
    const std::string_view allFilter(allSearchBuf_);

    if (cfg.blacklistEntries.empty()) {
        ImGui::BeginChild("##AllBlacklistEmpty", ImVec2(0.0f, part2H), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistEmpty));
        ImGui::EndChild();
        return;
    }

    // Order: current active server first, then alphabetically by server name, then player name
    std::vector<std::size_t> sortedIndices;
    sortedIndices.reserve(cfg.blacklistEntries.size());
    for (std::size_t i = 0; i < cfg.blacklistEntries.size(); ++i) {
        const auto& e = cfg.blacklistEntries[i];
        if (!containsIgnoreCase(e.playerName, allFilter) &&
            !containsIgnoreCase(e.serverName, allFilter) &&
            !containsIgnoreCase(e.serverEndpoint, allFilter)) {
            continue;
        }
        sortedIndices.push_back(i);
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](std::size_t ia, std::size_t ib) {
        const auto& a = cfg.blacklistEntries[ia];
        const auto& b = cfg.blacklistEntries[ib];
        const bool aCur = view.connected && !view.serverEndpoint.empty() &&
                          equalsIgnoreCase(a.serverEndpoint, view.serverEndpoint);
        const bool bCur = view.connected && !view.serverEndpoint.empty() &&
                          equalsIgnoreCase(b.serverEndpoint, view.serverEndpoint);
        if (aCur != bCur) {
            return aCur;
        }
        const int srvCmp = compareIgnoreCase(a.serverName, b.serverName);
        if (srvCmp != 0) {
            return srvCmp < 0;
        }
        const int plrCmp = compareIgnoreCase(a.playerName, b.playerName);
        if (plrCmp != 0) {
            return plrCmp < 0;
        }
        return compareIgnoreCase(a.serverEndpoint, b.serverEndpoint) < 0;
    });

    const char* originBtnTxt = loc.tr(TextKey::BlacklistScopeOriginBtn);
    const char* allBtnTxt    = loc.tr(TextKey::BlacklistMuteAllBtn);
    const char* removeBtnTxt = loc.tr(TextKey::BlacklistRemoveBtn);
    const float actionsColW  = calcBtnWidth(originBtnTxt) + calcBtnWidth(allBtnTxt) +
                               calcBtnWidth(removeBtnTxt) + style.ItemInnerSpacing.x * 2.0f + 8.0f;

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("##AllBlacklistTable", 5, kTableFlags, ImVec2(0.0f, part2H))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColPlayer),   ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColServer),   ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColEndpoint), ImGuiTableColumnFlags_WidthFixed, 134.0f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColScope),    ImGuiTableColumnFlags_WidthFixed, 128.0f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColActions),  ImGuiTableColumnFlags_WidthFixed, actionsColW);
        ImGui::TableHeadersRow();

        // Snapshot mutation targets after iteration to avoid vector invalidation mid-loop
        enum class RowAction { None, SetOrigin, SetAll, Remove };
        RowAction pendingAction = RowAction::None;
        std::string actionPlayer;
        std::string actionEp;

        for (std::size_t rowIdx : sortedIndices) {
            const auto& e = cfg.blacklistEntries[rowIdx];
            const bool isAll = (e.scope == config::BlacklistScope::All);
            const char* scopeLabel = isAll ? loc.tr(TextKey::BlacklistScopeAllLabel)
                                           : loc.tr(TextKey::BlacklistScopeServerLabel);

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(5000 + rowIdx));

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(e.playerName.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.serverName.empty() ? "-" : e.serverName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.serverEndpoint.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(scopeLabel);

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(!isAll);
            if (ImGui::SmallButton(originBtnTxt)) {
                pendingAction = RowAction::SetOrigin;
                actionPlayer  = e.playerName;
                actionEp      = e.serverEndpoint;
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::BeginDisabled(isAll);
            if (ImGui::SmallButton(allBtnTxt)) {
                pendingAction = RowAction::SetAll;
                actionPlayer  = e.playerName;
                actionEp      = e.serverEndpoint;
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            if (ImGui::SmallButton(removeBtnTxt)) {
                pendingAction = RowAction::Remove;
                actionPlayer  = e.playerName;
                actionEp      = e.serverEndpoint;
            }

            ImGui::PopID();
        }

        if (pendingAction == RowAction::SetOrigin) {
            if (cfg.setBlacklistEntryScope(actionPlayer, actionEp, config::BlacklistScope::Server)) {
                dirty = true;
            }
        } else if (pendingAction == RowAction::SetAll) {
            if (cfg.setBlacklistEntryScope(actionPlayer, actionEp, config::BlacklistScope::All)) {
                dirty = true;
            }
        } else if (pendingAction == RowAction::Remove) {
            if (cfg.removeBlacklistEntry(actionPlayer, actionEp)) {
                dirty = true;
            }
        }

        ImGui::EndTable();
    }
}

void ConfigMenu::renderBlacklistTab(config::ClientConfig& cfg, Localization& loc, bool& dirty) {
    const auto& samp = samp::SampBridge::instance();
    BlacklistTabView view;
    view.connected = samp.isConnected();
    if (view.connected) {
        view.serverEndpoint = samp.getServerEndpointKey();
        view.serverName     = samp.getServerHostname();
        const std::uint16_t localId = samp.getLocalPlayerId();
        for (std::uint16_t pid = 0; pid < 1004; ++pid) {
            if (pid == localId || !samp.isRemotePlayerConnected(pid)) {
                continue;
            }
            const std::string name = samp.getPlayerName(pid);
            if (!name.empty()) {
                view.onlinePlayers.push_back(BlacklistOnlinePlayerView{pid, name});
            }
        }
    }
    renderBlacklistTabWithState(cfg, loc, view, dirty);
}

} // namespace svplus::client::ui
