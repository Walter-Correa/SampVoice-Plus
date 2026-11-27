#include "ui/ConfigMenu.hpp"

#include "svplus/client/CaptureEngine.hpp"
#include "svplus/client/PlaybackEngine.hpp"
#include "svplus/client/VoiceClient.hpp"
#include "samp/SampBridge.hpp"
#include "svplus/common/Logger.hpp"
#include "svplus/common/Version.hpp"
#include "ui/D3D9Hook.hpp"
#include "ui/HudOverlay.hpp"
#include "util/Utf8Util.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
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

void fitUtf8TextToWidthInto(std::string_view text, float maxWidth, std::string& out) {
    out.assign(text);
    if (out.empty() || ImGui::CalcTextSize(out.c_str()).x <= maxWidth) {
        return;
    }
    if (maxWidth <= 0.0f) {
        out.assign("...");
        return;
    }
    std::size_t bytes = out.size();
    while (bytes > 0) {
        const std::size_t prev = util::utf8TruncateLen(text, bytes - 1u);
        out.assign(text.substr(0, prev));
        out.append("...");
        if (ImGui::CalcTextSize(out.c_str()).x <= maxWidth || prev == 0) {
            return;
        }
        bytes = prev;
    }
    out.assign("...");
}

std::string fitUtf8TextToWidth(std::string_view text, float maxWidth) {
    std::string out;
    fitUtf8TextToWidthInto(text, maxWidth, out);
    return out;
}

struct SliderArrowResult {
    bool changedByArrow{false};
    bool changedBySlider{false};
    bool sliderActivated{false};
    bool sliderDeactivatedAfterEdit{false};
    bool anyHovered{false};
};

template <typename T>
bool renderSteppedSliderControl(const char* id,
                                T& value,
                                T minVal,
                                T maxVal,
                                T step,
                                const char* fmt,
                                float totalWidth,
                                const char* rightLabel,
                                SliderArrowResult* outResult) {
    bool changed = false;
    SliderArrowResult detail{};
    const auto& style = ImGui::GetStyle();
    const float btnW = ImGui::GetFrameHeight();
    const float sliderW = (std::max)(60.0f, totalWidth - 2.0f * (btnW + style.ItemInnerSpacing.x));

    constexpr bool kIsFloat = std::is_floating_point_v<T>;
    const double stepD = (static_cast<double>(step) > 0.0) ? static_cast<double>(step) : (kIsFloat ? 0.01 : 1.0);
    const double minD  = static_cast<double>(minVal);
    const double maxD  = static_cast<double>(maxVal);
    const int totalSteps = (std::max)(1, static_cast<int>(std::llround((maxD - minD) / stepD)));

    auto snapRaw = [&](double raw) noexcept -> T {
        const double q = minD + std::round((raw - minD) / stepD) * stepD;
        if constexpr (kIsFloat) {
            const float clamped = std::clamp(static_cast<float>(q), minVal, maxVal);
            return static_cast<float>(std::round(static_cast<double>(clamped) * 100.0) / 100.0);
        } else {
            return std::clamp(static_cast<int>(std::llround(q)), minVal, maxVal);
        }
    };
    auto valToStep = [&](T v) noexcept -> int {
        return std::clamp(static_cast<int>(std::llround((static_cast<double>(v) - minD) / stepD)), 0, totalSteps);
    };
    auto stepToVal = [&](int s) noexcept -> T {
        return snapRaw(minD + static_cast<double>(std::clamp(s, 0, totalSteps)) * stepD);
    };

    value = snapRaw(static_cast<double>(value));

    ImGui::PushID(id);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) {
        const int nextStep = (std::max)(0, valToStep(value) - 1);
        const T next = stepToVal(nextStep);
        if (next != value) {
            value = next;
            changed = true;
            detail.changedByArrow = true;
        }
    }
    detail.anyHovered = detail.anyHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopButtonRepeat();

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window && !window->SkipItems) {
            ImGuiContext& g = *GImGui;
            const ImGuiID sliderId = window->GetID("##slider");
            const ImRect frameBb(
                window->DC.CursorPos,
                ImVec2(window->DC.CursorPos.x + sliderW,
                       window->DC.CursorPos.y + ImGui::GetFontSize() + style.FramePadding.y * 2.0f));
            ImGui::ItemSize(frameBb, style.FramePadding.y);
            if (ImGui::ItemAdd(frameBb, sliderId, &frameBb, ImGuiItemFlags_Inputable)) {
                const bool hovered = ImGui::ItemHoverable(frameBb, sliderId, g.LastItemData.ItemFlags);
                detail.anyHovered = detail.anyHovered || hovered ||
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

                bool tempInputActive = ImGui::TempInputIsActive(sliderId);
                if (!tempInputActive) {
                    const bool clicked = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, sliderId);
                    const bool makeActive = (clicked || g.NavActivateId == sliderId);
                    if (makeActive && clicked) {
                        ImGui::SetKeyOwner(ImGuiKey_MouseLeft, sliderId);
                    }
                    if (makeActive && ((clicked && g.IO.KeyCtrl) ||
                        (g.NavActivateId == sliderId && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))) {
                        tempInputActive = true;
                    }
                    if (makeActive && !tempInputActive) {
                        ImGui::SetActiveID(sliderId, window);
                        ImGui::SetFocusID(sliderId, window);
                        ImGui::FocusWindow(window);
                        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
                    }
                }

                if (tempInputActive) {
                    double dVal = static_cast<double>(value);
                    const bool edited = ImGui::TempInputScalar(
                        frameBb, sliderId, "##slider", ImGuiDataType_Double, &dVal,
                        kIsFloat ? "%.2f" : "%.0f", nullptr, nullptr);
                    if (edited) {
                        const T next = snapRaw(dVal);
                        if (next != value) {
                            value = next;
                            changed = true;
                            detail.changedBySlider = true;
                            ImGui::MarkItemEdited(sliderId);
                        }
                    }
                } else {
                    const ImU32 frameCol = ImGui::GetColorU32(
                        g.ActiveId == sliderId ? ImGuiCol_FrameBgActive
                        : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
                    ImGui::RenderNavCursor(frameBb, sliderId);
                    ImGui::RenderFrame(frameBb.Min, frameBb.Max, frameCol, true, style.FrameRounding);

                    constexpr float kGrabPadding = 2.0f;
                    const float sliderSz = (frameBb.Max.x - frameBb.Min.x) - kGrabPadding * 2.0f;
                    float grabSz = kIsFloat
                        ? style.GrabMinSize
                        : (std::max)(sliderSz / (static_cast<float>(totalSteps) + 1.0f), style.GrabMinSize);
                    grabSz = (std::min)(grabSz, (std::max)(1.0f, sliderSz));
                    const float usableSz  = (std::max)(1.0f, sliderSz - grabSz);
                    const float usableMin = frameBb.Min.x + kGrabPadding + grabSz * 0.5f;
                    const float usableMax = usableMin + usableSz;

                    const int curStep = valToStep(value);
                    if (g.ActiveId == sliderId) {
                        if (g.ActiveIdSource == ImGuiInputSource_Mouse) {
                            if (!g.IO.MouseDown[0]) {
                                ImGui::ClearActiveID();
                            } else {
                                const float mouseX = g.IO.MousePos.x;
                                const int mousePx = static_cast<int>(std::lround(mouseX));
                                static ImGuiID sActiveDragId = 0;
                                static int     sLastMousePx = 0;
                                static float   sGrabOffset = 0.0f;
                                static bool    sRelativeMode = false;

                                if (g.ActiveIdIsJustActivated || sActiveDragId != sliderId) {
                                    const float grabT = static_cast<float>(curStep) / static_cast<float>(totalSteps);
                                    const float grabPos = ImLerp(usableMin, usableMax, grabT);
                                    const bool clickedGrab =
                                        (mouseX >= grabPos - grabSz * 0.5f - 1.0f) &&
                                        (mouseX <= grabPos + grabSz * 0.5f + 1.0f);
                                    sActiveDragId = sliderId;
                                    sRelativeMode = (totalSteps > static_cast<int>(std::floor(usableSz)));
                                    sGrabOffset   = (clickedGrab && !sRelativeMode && kIsFloat) ? (mouseX - grabPos) : 0.0f;
                                    sLastMousePx  = mousePx;
                                    if (!clickedGrab) {
                                        const float t = std::clamp((mouseX - usableMin) / usableSz, 0.0f, 1.0f);
                                        const int nextStep = std::clamp(
                                            static_cast<int>(std::lround(t * static_cast<float>(totalSteps))),
                                            0, totalSteps);
                                        if (nextStep != curStep) {
                                            value = stepToVal(nextStep);
                                            changed = true;
                                            detail.changedBySlider = true;
                                            ImGui::MarkItemEdited(sliderId);
                                        }
                                    }
                                } else if (sRelativeMode) {
                                    const int deltaPx = mousePx - sLastMousePx;
                                    sLastMousePx = mousePx;
                                    if (deltaPx != 0) {
                                        const int nextStep = std::clamp(curStep + deltaPx, 0, totalSteps);
                                        if (nextStep != curStep) {
                                            value = stepToVal(nextStep);
                                            changed = true;
                                            detail.changedBySlider = true;
                                            ImGui::MarkItemEdited(sliderId);
                                        }
                                    }
                                } else {
                                    const float t = std::clamp((mouseX - sGrabOffset - usableMin) / usableSz, 0.0f, 1.0f);
                                    const int nextStep = std::clamp(
                                        static_cast<int>(std::lround(t * static_cast<float>(totalSteps))),
                                        0, totalSteps);
                                    if (nextStep != curStep) {
                                        value = stepToVal(nextStep);
                                        changed = true;
                                        detail.changedBySlider = true;
                                        ImGui::MarkItemEdited(sliderId);
                                    }
                                }
                            }
                        } else if (g.ActiveIdSource == ImGuiInputSource_Keyboard ||
                                   g.ActiveIdSource == ImGuiInputSource_Gamepad) {
                            if (g.NavActivatePressedId == sliderId && !g.ActiveIdIsJustActivated) {
                                ImGui::ClearActiveID();
                            } else {
                                const float navDelta = ImGui::GetNavTweakPressedAmount(ImGuiAxis_X);
                                if (navDelta != 0.0f) {
                                    const int stepDelta = (navDelta > 0.0f) ? 1 : -1;
                                    const int nextStep = std::clamp(curStep + stepDelta, 0, totalSteps);
                                    if (nextStep != curStep) {
                                        value = stepToVal(nextStep);
                                        changed = true;
                                        detail.changedBySlider = true;
                                        ImGui::MarkItemEdited(sliderId);
                                    }
                                }
                            }
                        }
                    }

                    if (sliderSz >= 1.0f) {
                        const int drawStep = valToStep(value);
                        const float grabT = static_cast<float>(drawStep) / static_cast<float>(totalSteps);
                        const float grabPos = ImLerp(usableMin, usableMax, grabT);
                        const ImRect grabBb(
                            grabPos - grabSz * 0.5f, frameBb.Min.y + kGrabPadding,
                            grabPos + grabSz * 0.5f, frameBb.Max.y - kGrabPadding);
                        if (grabBb.Max.x > grabBb.Min.x) {
                            window->DrawList->AddRectFilled(
                                grabBb.Min, grabBb.Max,
                                ImGui::GetColorU32(g.ActiveId == sliderId ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab),
                                style.GrabRounding);
                        }
                    }

                    char valBuf[64]{};
                    const int len = std::snprintf(valBuf, sizeof(valBuf), fmt ? fmt : (kIsFloat ? "%.2f" : "%d"), value);
                    const char* valEnd = (len > 0) ? (valBuf + (std::min)(len, static_cast<int>(sizeof(valBuf) - 1))) : valBuf;
                    ImGui::RenderTextClipped(frameBb.Min, frameBb.Max, valBuf, valEnd, nullptr, ImVec2(0.5f, 0.5f));
                }
            }
        }
        detail.sliderActivated            = ImGui::IsItemActivated();
        detail.sliderDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) {
        const int nextStep = (std::min)(totalSteps, valToStep(value) + 1);
        const T next = stepToVal(nextStep);
        if (next != value) {
            value = next;
            changed = true;
            detail.changedByArrow = true;
        }
    }
    detail.anyHovered = detail.anyHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopButtonRepeat();

    if (rightLabel && rightLabel[0] != '\0') {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(rightLabel);
        detail.anyHovered = detail.anyHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    }
    ImGui::PopID();
    if (outResult) {
        *outResult = detail;
    }
    return changed;
}

bool sliderFloatWithArrows(const char* id,
                           float& value,
                           float minVal,
                           float maxVal,
                           float step,
                           const char* fmt,
                           float totalWidth,
                           const char* rightLabel = nullptr,
                           SliderArrowResult* outResult = nullptr) {
    return renderSteppedSliderControl<float>(id, value, minVal, maxVal, step, fmt, totalWidth, rightLabel, outResult);
}

bool sliderIntWithArrows(const char* id,
                         int& value,
                         int minVal,
                         int maxVal,
                         int step,
                         const char* fmt,
                         float totalWidth,
                         const char* rightLabel = nullptr,
                         SliderArrowResult* outResult = nullptr) {
    return renderSteppedSliderControl<int>(id, value, minVal, maxVal, step, fmt, totalWidth, rightLabel, outResult);
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
        if (dl) {
            dl->AddRectFilled(ImVec2(x, yTop), ImVec2(x + barW, yBot), col, barW * 0.5f);
        }
        x += barW + barGap;
    }
    x += 5.0f;

    if (!titleFont) {
        char fallback[96]{};
        std::snprintf(fallback, sizeof(fallback), "SampVoice Plus %s", verText);
        const ImVec2 sz = ImGui::CalcTextSize(fallback);
        const float fbY = origin.y + (brandH - sz.y) * 0.5f;
        const float fbBaseline = fbY + (bodyFont ? bodyFont->Ascent * (bodySize / bodyFont->FontSize) : bodySize * 0.8f);
        if (dl) {
            dl->AddText(ImVec2(x, fbY), kColTop, fallback);
        }
        return {ImVec2((x - origin.x) + sz.x, std::max(brandH, sz.y)), fbBaseline};
    }

    // 2. "SampVoice" in Noto Sans Black with drop shadow + vertical gradient (never extends above origin.y)
    const float titleY = origin.y + (brandH - titleSz.y) * 0.5f;
    const ImVec2 titlePos(x, titleY);

    if (dl) {
        dl->AddText(titleFont, titlePx, ImVec2(titlePos.x + 1.5f, titlePos.y + 1.5f), kColShadow, kBrandName);
        const int vertStart = dl->VtxBuffer.Size;
        dl->AddText(titleFont, titlePx, titlePos, IM_COL32_WHITE, kBrandName);
        const int vertEnd = dl->VtxBuffer.Size;
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(
            dl, vertStart, vertEnd,
            titlePos, ImVec2(titlePos.x, titlePos.y + titleSz.y),
            kColTop, kColBot);
    }
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

    if (dl) {
        dl->AddRectFilled(ImVec2(x, pillY), ImVec2(x + pillW, pillY + pillH), kColPillBg, pillH * 0.5f);
        dl->AddText(smallFont, smallPx, ImVec2(x + pillPadX, pillTextY), kColPillFg, kPillText);
    }
    x += pillW + 7.0f;

    // 4. Version tag ("v1.x.x") using crisp 12 px badgeFont, baseline-aligned
    const ImVec2 verSz = smallFont->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, verText);
    if (dl) {
        dl->AddText(smallFont, smallPx, ImVec2(x, titleBaseline - smallAscent), verCol, verText);
    }
    x += verSz.x;

    return {ImVec2(x - origin.x, brandH), dl ? titleBaseline : fallbackBaseline};
}

void drawOverheadPreviewBox(ImTextureID speakerTex, int opacityPct, float uiAlpha, bool drawGfx) noexcept {
    const float boxSize = 32.0f;
    ImGui::Dummy(ImVec2(boxSize, boxSize));
    if (!drawGfx) {
        return;
    }
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

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

void drawRealSizeIconPreview(ImTextureID tex, float sizePx, float maxBoxSizePx, float uiAlpha, bool drawGfx) noexcept {
    const float maxBox = std::clamp(maxBoxSizePx, 10.0f, 128.0f);
    const float clamped = std::clamp(sizePx, 10.0f, maxBox);
    ImGui::Dummy(ImVec2(maxBox, maxBox));
    if (!drawGfx || !tex) {
        return;
    }
    const ImVec2 bMin = ImGui::GetItemRectMin();
    const ImVec2 bMax = ImGui::GetItemRectMax();
    const ImVec2 center((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
    const float half = clamped * 0.5f;
    const int alpha = static_cast<int>(std::lround(255.0f * uiAlpha));
    ImGui::GetWindowDrawList()->AddImage(
        tex,
        ImVec2(center.x - half, center.y - half),
        ImVec2(center.x + half, center.y + half),
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32(255, 255, 255, alpha));
}

void placeRightOnSameLine(float spanWidth, float itemWidth) noexcept {
    const auto& style = ImGui::GetStyle();
    const float prevEndScreen   = ImGui::GetItemRectMax().x;
    const float lineRightScreen = ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x + spanWidth;
    const float extraSpacing    = (std::max)(style.ItemSpacing.x, lineRightScreen - prevEndScreen - itemWidth);
    ImGui::SameLine(0.0f, extraSpacing);
}

struct MenuSizeConstraintData {
    float minW{0.0f};
    float wideW{0.0f};
    float minNarrowH{0.0f};
    float minWideH{0.0f};
    float maxW{0.0f};
    float maxH{0.0f};
};

void menuSizeConstraintCallback(ImGuiSizeCallbackData* data) {
    if (!data || !data->UserData) {
        return;
    }
    const auto* c = static_cast<const MenuSizeConstraintData*>(data->UserData);
    data->DesiredSize.x = std::clamp(data->DesiredSize.x, c->minW, c->maxW);
    const float minH = (data->DesiredSize.x + 0.5f >= c->wideW) ? c->minWideH : c->minNarrowH;
    data->DesiredSize.y = std::clamp(data->DesiredSize.y, minH, c->maxH);
}

bool isActivelyResizingWindow(ImGuiWindow* win) noexcept {
    if (!win) {
        return false;
    }
    if (win->ResizeBorderHeld != -1) {
        return true;
    }
    const ImGuiID activeId = ImGui::GetActiveID();
    if (activeId == 0) {
        return false;
    }
    for (int corner = 0; corner < 4; ++corner) {
        if (ImGui::GetWindowResizeCornerID(win, corner) == activeId) {
            return true;
        }
    }
    for (int border = 0; border < 4; ++border) {
        if (ImGui::GetWindowResizeBorderID(win, static_cast<ImGuiDir>(border)) == activeId) {
            return true;
        }
    }
    return false;
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
        safetyBoostW_          = 0.0f;
        safetyBoostH_          = 0.0f;
        settledSameSizeFrames_ = 0;
        prevObservedWinSize_   = ImVec2(0.0f, 0.0f);
        pendingCondRemeasure_  = false;
        loggedOverflowWarn_    = {};
    }
    open_ = open;
    if (!open) {
        anyPopupOpenLastFrame_ = false;
        closePopupOnNextFrame_ = false;
        volDragActive_         = false;
        capturingHotkey_ = false;
        capturingDeafenHotkey_ = false;
        deafenHotkeyMenuConflict_ = false;
        auto& capture = audio::CaptureEngine::instance();
        if (capture.isMicMonitorEnabled()) {
            capture.setMicMonitorEnabled(false, "menu_closed");
        }
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

bool ConfigMenu::handleWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam,
                                     config::ClientConfig& cfg,
                                     bool chatOrGameMenuOpen,
                                     bool& outConfigDirty) {
    if (isNeverConsumedSystemMessage(msg, wParam)) {
        const bool lostFocus =
            (msg == WM_KILLFOCUS) ||
            (msg == WM_ACTIVATEAPP && wParam == FALSE) ||
            (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE);
        if (lostFocus && open_) {
            setOpen(false);
        }
        return false;
    }

    std::uint32_t pressedVk = 0;
    bool isKeyDownEvent = false;
    const bool capturingAny = capturingHotkey_ || capturingDeafenHotkey_;

    if (msg == WM_KEYDOWN) {
        if ((lParam & (1 << 30)) == 0) {
            pressedVk = static_cast<std::uint32_t>(wParam);
            isKeyDownEvent = true;
        }
    } else if (msg == WM_MBUTTONDOWN) {
        if (capturingAny || (!chatOrGameMenuOpen && (cfg.menuHotkey == VK_MBUTTON || cfg.deafenHotkey == VK_MBUTTON))) {
            pressedVk = VK_MBUTTON;
            isKeyDownEvent = true;
        }
    } else if (msg == WM_XBUTTONDOWN) {
        const std::uint32_t xvk = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        if (capturingAny || (!chatOrGameMenuOpen && (cfg.menuHotkey == xvk || cfg.deafenHotkey == xvk))) {
            pressedVk = xvk;
            isKeyDownEvent = true;
        }
    }

    if (capturingHotkey_ && isKeyDownEvent && pressedVk != 0) {
        capturingHotkey_ = false;
        if (pressedVk != VK_ESCAPE) {
            cfg.menuHotkey = pressedVk;
            if (cfg.deafenHotkey == cfg.menuHotkey) {
                cfg.deafenHotkey = 0;
                SVPLUS_LOG_INF_CAT("config", "Cleared deafen_hotkey after binding menu_hotkey to same key 0x%02X",
                                   static_cast<unsigned>(pressedVk));
            }
            outConfigDirty = true;
        }
        return true;
    }

    if (capturingDeafenHotkey_ && isKeyDownEvent && pressedVk != 0) {
        capturingDeafenHotkey_ = false;
        if (pressedVk != VK_ESCAPE) {
            if (pressedVk == cfg.menuHotkey) {
                deafenHotkeyMenuConflict_ = true;
                SVPLUS_LOG_WRN_CAT("ui", "Rejected deafen hotkey 0x%02X (conflicts with menu hotkey)",
                                   static_cast<unsigned>(pressedVk));
            } else {
                deafenHotkeyMenuConflict_ = false;
                if (cfg.deafenHotkey != pressedVk) {
                    cfg.deafenHotkey = pressedVk;
                    outConfigDirty = true;
                }
            }
        }
        return true;
    }

    if (isKeyDownEvent && cfg.deafenHotkey != 0 && pressedVk == cfg.deafenHotkey && !chatOrGameMenuOpen) {
        auto& playback = audio::PlaybackEngine::instance();
        playback.setDeafened(!playback.isDeafened(), "hotkey");
        return true;
    }

    if (isKeyDownEvent && pressedVk == cfg.menuHotkey && !chatOrGameMenuOpen) {
        setOpen(!open_);
        return true;
    }

    if (open_ && isKeyDownEvent && pressedVk == VK_ESCAPE) {
        const bool popupOpen =
            anyPopupOpenLastFrame_ ||
            (ImGui::GetCurrentContext() != nullptr && GImGui != nullptr && GImGui->OpenPopupStack.Size > 0);
        if (popupOpen) {
            closePopupOnNextFrame_ = true;
            return true;
        }
        setOpen(false);
        return true;
    }

    return open_ && isMenuUiInputMessage(msg);
}

bool ConfigMenu::shouldAnimateBrandHeader(const BrandAnimationInputs& in) noexcept {
    if (in.measuring) {
        return false;
    }
    if (in.transmitting) {
        return true;
    }
    if (in.deafened || !in.soundEnable || in.soundVolume <= 0 || in.listenerDisabled || !in.speakers) {
        return false;
    }
    for (const auto& sp : *in.speakers) {
        if (!sp.blocked) {
            return true;
        }
    }
    return false;
}

void ConfigMenu::renderChromeHeader(const config::ClientConfig& cfg,
                                    Localization& loc,
                                    MenuLayout& layout) const {
    const auto& io = ImGui::GetIO();
    const ImVec2 brandOrigin = ImGui::GetCursorScreenPos();
    const auto& voiceClient = net::VoiceClient::instance();
    const auto speakersSnap = layout.measuring()
        ? nullptr
        : audio::PlaybackEngine::instance().activeSpeakersSnapshot();
    const bool isTalkingOrListening = shouldAnimateBrandHeader(BrandAnimationInputs{
        layout.measuring(),
        audio::CaptureEngine::instance().isTransmitting(),
        audio::PlaybackEngine::instance().isDeafened(),
        cfg.soundEnable,
        cfg.soundVolume,
        voiceClient.isListenerDisabled(),
        speakersSnap.get()
    });
    ImFont* titleFnt = D3D9Hook::instance().titleFont();
    ImFont* badgeFnt = D3D9Hook::instance().badgeFont();
    if (!titleFnt && io.Fonts && io.Fonts->Fonts.Size >= 3) {
        titleFnt = io.Fonts->Fonts[1];
        badgeFnt = io.Fonts->Fonts[2];
    }
    const BrandHeaderMetrics brand = drawBrandHeader(
        layout.measuring() ? nullptr : ImGui::GetWindowDrawList(),
        brandOrigin,
        titleFnt,
        badgeFnt,
        isTalkingOrListening);
    ImGui::Dummy(brand.size);
    ImGui::SameLine(0.0f, 18.0f);

    const auto& samp        = samp::SampBridge::instance();
    const bool connected    = voiceClient.isVoiceConnected();

    ImFont* bodyFnt = ImGui::GetFont();
    const float bodyPx = ImGui::GetFontSize();
    const float bodyAscent = bodyFnt
        ? (bodyFnt->Ascent * (bodyPx / bodyFnt->FontSize))
        : (bodyPx * 0.8f);
    const float groupTopY = ImGui::GetCursorScreenPos().y;
    const float textVertOffset = (std::max)(0.0f, brand.titleBaseline - bodyAscent - groupTopY);

    char actualVer[160]{};
    if (connected && voiceClient.rttMs() >= 0.0f) {
        std::snprintf(actualVer, sizeof(actualVer), "%s: %s (%s)  |  %s: %.0f ms",
                      loc.tr(TextKey::StatusSampVersion),
                      samp.versionName(),
                      voiceClient.isServerPlus() ? "v12" : "v11",
                      loc.tr(TextKey::StatusRttMs),
                      voiceClient.rttMs());
    } else {
        std::snprintf(actualVer, sizeof(actualVer), "%s: %s (%s)",
                      loc.tr(TextKey::StatusSampVersion),
                      samp.versionName(),
                      voiceClient.isServerPlus() ? "v12" : "v11");
    }
    char longestVer[160]{};
    std::snprintf(longestVer, sizeof(longestVer), "%s: 0.3.7-R4-2 (v12)  |  %s: 999 ms",
                  loc.tr(TextKey::StatusSampVersion),
                  loc.tr(TextKey::StatusRttMs));

    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textVertOffset);
    ImGui::TextDisabled("%s", layout.longestText(actualVer, {actualVer, longestVer}));
    ImGui::EndGroup();

    ImGui::Separator();

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
        char udpBlockedMax[256]{};
        std::snprintf(udpBlockedMax, sizeof(udpBlockedMax), loc.tr(TextKey::DiagUdpBlocked), 65535u);
        const char* statusText = layout.longestText(
            diagBuf,
            {
                loc.tr(TextKey::DiagConnectedOk),
                loc.tr(TextKey::DiagConnectedMicDisabled),
                loc.tr(TextKey::DiagConnectedNoMic),
                loc.tr(TextKey::DiagConnectedMuted),
                loc.tr(TextKey::DiagListenerDisabled),
                loc.tr(TextKey::DiagConnecting),
                udpBlockedMax,
                loc.tr(TextKey::DiagNoVoiceServer),
                loc.tr(TextKey::DiagIdle),
            });

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", loc.tr(TextKey::StatusLabel));
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(diagCol, "%s", statusText);
    }

    const bool hasConflict = audio::CaptureEngine::instance().hasKeyConflict();
    if (layout.visible(hasConflict)) {
        const std::string actualVk = config::formatVirtualKeyName(
            hasConflict ? audio::CaptureEngine::instance().conflictingKey() : 0x7Au);
        const char* vkText = layout.longestText(
            actualVk.c_str(),
            {"Mouse Mid", "Backspace", "CapsLock", "PageDown", "Key 0xFF"});
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            loc.tr(TextKey::HotkeyConflictWarning),
            vkText);
    }
}

void ConfigMenu::renderChromeFooter(config::ClientConfig& cfg,
                                    Localization& loc,
                                    MenuLayout& layout,
                                    bool& dirty) {
    const auto& style = ImGui::GetStyle();
    ImGui::Separator();

    const char* hintTxt    = loc.tr(TextKey::FooterHint);
    const char* autoBtnTxt = loc.tr(TextKey::AutoSizeButton);
    const float autoBtnW   = ImGui::CalcTextSize(autoBtnTxt).x + style.FramePadding.x * 2.0f;

    const bool showAutoBtn = layout.visible(cfg.menuSizeManual);
    const float rowAvailW = layout.availWidth();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", hintTxt);
    if (showAutoBtn) {
        if (layout.measuring() && layout.passWidth() <= 0.0f) {
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
        } else {
            placeRightOnSameLine(rowAvailW, autoBtnW);
        }
        if (ImGui::Button(autoBtnTxt) && !layout.measuring()) {
            cfg.menuSizeManual = false;
            forceApplyAutoSize_ = true;
            dirty = true;
            SVPLUS_LOG_INF_CAT("ui", "Clicked Auto size button -> resetting to automatic menu dimensions");
        }
        if (!layout.measuring() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", loc.tr(TextKey::AutoSizeTooltip));
        }
    }
}

void ConfigMenu::render(config::ClientConfig& cfg,
                        Localization& loc,
                        const UiTextures& textures,
                        bool& outConfigDirty) {
    renderWithExtraTabs(cfg, loc, textures, {}, outConfigDirty);
}

void ConfigMenu::renderWithExtraTabs(config::ClientConfig& cfg,
                                     Localization& loc,
                                     const UiTextures& textures,
                                     std::span<const MenuTabSpec> extraTabs,
                                     bool& outConfigDirty,
                                     const std::function<void(MenuLayout&)>& extraGeneralRight) {
    if (!open_) {
        return;
    }

    if (closePopupOnNextFrame_) {
        closePopupOnNextFrame_ = false;
        if (GImGui != nullptr && GImGui->OpenPopupStack.Size > 0) {
            ImGui::ClosePopupToLevel(GImGui->OpenPopupStack.Size - 1, true);
        }
    }

    const auto& io = ImGui::GetIO();
    const float dispW = (std::max)(1.0f, io.DisplaySize.x);
    const float dispH = (std::max)(1.0f, io.DisplaySize.y);

    bool dirty = false;

    std::vector<MenuTabSpec> tabs;
    tabs.reserve(4u + extraTabs.size());
    tabs.push_back(MenuTabSpec{
        "TabGeneral",
        loc.tr(TextKey::TabGeneral),
        [&](MenuLayout& l) { renderGeneralTab(cfg, loc, textures, l, dirty, extraGeneralRight); }
    });
    tabs.push_back(MenuTabSpec{
        "TabAudio",
        loc.tr(TextKey::TabAudio),
        [&](MenuLayout& l) { renderAudioTab(cfg, loc, l, dirty); }
    });
    tabs.push_back(MenuTabSpec{
        "TabMicrophone",
        loc.tr(TextKey::TabMicrophone),
        [&](MenuLayout& l) { renderMicrophoneTab(cfg, loc, l, dirty); }
    });
    tabs.push_back(MenuTabSpec{
        "TabBlacklist",
        loc.tr(TextKey::TabBlacklist),
        [&](MenuLayout& l) { renderBlacklistTab(cfg, loc, textures, l, dirty); }
    });
    for (const auto& extra : extraTabs) {
        tabs.push_back(extra);
    }

    const auto now = std::chrono::steady_clock::now();
    const bool langChanged = (lastMeasuredLang_ != loc.activeLanguage());
    const bool extraChanged = (extraTabs.size() != lastExtraTabCount_);
    const bool hasExtraRight = static_cast<bool>(extraGeneralRight);
    const bool extraRightChanged = (hasExtraRight != lastHasExtraGeneralRight_);
    const bool condRemeasure = pendingCondRemeasure_;
    const bool intervalDue = !hasMeasured_ || (now - lastMeasureTime_) >= std::chrono::milliseconds(250);
    if (applyingOpeningLayout_ || langChanged || extraChanged || extraRightChanged || condRemeasure || intervalDue) {
        if (langChanged || extraChanged || extraRightChanged) {
            safetyBoostW_ = 0.0f;
            safetyBoostH_ = 0.0f;
        }
        measured_ = measureMenuWindowDimensions(
            tabs,
            [&](MenuLayout& l) { renderChromeHeader(cfg, loc, l); },
            [&](MenuLayout& l) { renderChromeFooter(cfg, loc, l, dirty); });
        hasMeasured_              = true;
        lastMeasuredLang_         = loc.activeLanguage();
        lastExtraTabCount_        = extraTabs.size();
        lastHasExtraGeneralRight_ = hasExtraRight;
        lastMeasureTime_          = now;
        pendingCondRemeasure_     = false;
    }

    const float reqMinW        = measured_.minSize.x + safetyBoostW_;
    const float reqMinNarrowH  = measured_.minSize.y + safetyBoostH_;
    const float baseMinWideH   = (measured_.minHeightAtWide > 0.0f) ? measured_.minHeightAtWide : measured_.minSize.y;
    const float reqMinWideH    = baseMinWideH + safetyBoostH_;
    const float reqPrefW       = (std::max)(reqMinW, measured_.preferredSize.x + safetyBoostW_);
    const float reqPrefH       = (std::max)(reqMinWideH, measured_.preferredSize.y + safetyBoostH_);

    const ImVec2 maxAllowed(dispW * 0.98f, dispH * 0.98f);
    const bool minExceedsScreen =
        (reqMinW > maxAllowed.x + 0.5f) ||
        (reqMinWideH > maxAllowed.y + 0.5f) ||
        (reqMinNarrowH > maxAllowed.y + 0.5f && reqPrefW > maxAllowed.x + 0.5f);

    MenuSizeConstraintData constraintData{};
    ImVec2 minConstraint{};
    ImVec2 maxConstraint{};
    if (minExceedsScreen) {
        constraintData.minW       = (std::min)(reqMinW, dispW);
        constraintData.wideW      = (std::min)(reqPrefW, dispW);
        constraintData.minNarrowH = (std::min)(reqMinNarrowH, dispH);
        constraintData.minWideH   = (std::min)(reqMinWideH, dispH);
        constraintData.maxW       = dispW;
        constraintData.maxH       = dispH;
        minConstraint = ImVec2(constraintData.minW, (std::min)(constraintData.minNarrowH, constraintData.minWideH));
        maxConstraint = ImVec2(dispW, dispH);
        if (!loggedSmallScreenInf_) {
            loggedSmallScreenInf_ = true;
            const float exceededH = (reqPrefW > maxAllowed.x + 0.5f) ? reqMinNarrowH : reqMinWideH;
            SVPLUS_LOG_INF_CAT(
                "ui",
                "Menu minimum size %.0fx%.0f exceeds screen %.0fx%.0f (lang=%s); using full screen with scrollbars",
                reqMinW, exceededH, dispW, dispH, Localization::codeOf(loc.activeLanguage()));
        }
    } else {
        constraintData.minW       = reqMinW;
        constraintData.wideW      = reqPrefW;
        constraintData.minNarrowH = (std::min)(reqMinNarrowH, maxAllowed.y);
        constraintData.minWideH   = (std::min)(reqMinWideH, maxAllowed.y);
        constraintData.maxW       = maxAllowed.x;
        constraintData.maxH       = maxAllowed.y;
        minConstraint = ImVec2(constraintData.minW, (std::min)(constraintData.minNarrowH, constraintData.minWideH));
        maxConstraint = maxAllowed;
    }
    ImGui::SetNextWindowSizeConstraints(minConstraint, maxConstraint, &menuSizeConstraintCallback, &constraintData);

    float desiredW = minExceedsScreen
        ? dispW
        : ((cfg.menuSizeManual && cfg.menuW > 0.0f) ? std::round(cfg.menuW * dispW) : reqPrefW);
    desiredW = std::clamp(desiredW, constraintData.minW, constraintData.maxW);
    const float minHForDesiredW =
        (desiredW + 0.5f >= constraintData.wideW) ? constraintData.minWideH : constraintData.minNarrowH;

    float desiredH = minExceedsScreen
        ? dispH
        : ((cfg.menuSizeManual && cfg.menuH > 0.0f) ? std::round(cfg.menuH * dispH) : reqPrefH);
    desiredH = std::clamp(desiredH, minHForDesiredW, constraintData.maxH);

    const bool autoNeedsApply =
        !cfg.menuSizeManual &&
        (std::fabs(desiredW - lastAppliedCodeSize_.x) > 2.0f ||
         std::fabs(desiredH - lastAppliedCodeSize_.y) > 2.0f);
    const bool manualNeedsClampApply =
        cfg.menuSizeManual && cfg.menuW > 0.0f && cfg.menuH > 0.0f &&
        (std::fabs(desiredW - cfg.menuW * dispW) > 1.5f ||
         std::fabs(desiredH - cfg.menuH * dispH) > 1.5f);

    const bool programmaticSize =
        applyingOpeningLayout_ || forceApplyAutoSize_ || safetyNetForceSize_ ||
        autoNeedsApply || manualNeedsClampApply;

    if (programmaticSize) {
        ImGui::SetNextWindowSize(ImVec2(desiredW, desiredH), ImGuiCond_Always);
    } else if (!hasMeasured_) {
        ImGui::SetNextWindowSize(ImVec2(desiredW, desiredH), ImGuiCond_FirstUseEver);
    }

    if (applyingOpeningLayout_) {
        const float targetX = (cfg.menuX >= 0.0f)
            ? std::clamp(cfg.menuX * dispW, 0.0f, (std::max)(0.0f, dispW - desiredW))
            : (std::max)(0.0f, (dispW - desiredW) * 0.5f);
        const float targetY = (cfg.menuY >= 0.0f)
            ? std::clamp(cfg.menuY * dispH, 0.0f, (std::max)(0.0f, dispH - desiredH))
            : (std::max)(0.0f, (dispH - desiredH) * 0.5f);
        ImGui::SetNextWindowPos(ImVec2(targetX, targetY), ImGuiCond_Always);
        expectedWindowPos_ = ImVec2(targetX, targetY);
    } else if (programmaticSize) {
        const float clampedX = std::clamp(expectedWindowPos_.x, 0.0f, (std::max)(0.0f, dispW - desiredW));
        const float clampedY = std::clamp(expectedWindowPos_.y, 0.0f, (std::max)(0.0f, dispH - desiredH));
        if (std::fabs(clampedX - expectedWindowPos_.x) > 0.5f ||
            std::fabs(clampedY - expectedWindowPos_.y) > 0.5f) {
            ImGui::SetNextWindowPos(ImVec2(clampedX, clampedY), ImGuiCond_Always);
            expectedWindowPos_ = ImVec2(clampedX, clampedY);
            if (cfg.menuX >= 0.0f || cfg.menuY >= 0.0f) {
                cfg.menuX = std::clamp(clampedX / dispW, 0.0f, 1.0f);
                cfg.menuY = std::clamp(clampedY / dispH, 0.0f, 1.0f);
                dirty = true;
            }
        }
    }

    const bool wasOpening = applyingOpeningLayout_;
    applyingOpeningLayout_ = false;
    forceApplyAutoSize_    = false;
    safetyNetForceSize_    = false;

    bool keepOpen = open_;
    char titleBuf[256]{};
    std::snprintf(titleBuf, sizeof(titleBuf), "%s###SvPlusConfig", loc.tr(TextKey::WindowTitle));

    const ImGuiWindowFlags winFlags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        (minExceedsScreen
             ? ImGuiWindowFlags_HorizontalScrollbar
             : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));

    if (ImGui::Begin(titleBuf, &keepOpen, winFlags)) {
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();

        if (wasOpening) {
            lastAppliedCodeSize_ = winSize;
            expectedWindowPos_   = winPos;
        } else {
            if (std::fabs(winPos.x - expectedWindowPos_.x) > 1.5f ||
                std::fabs(winPos.y - expectedWindowPos_.y) > 1.5f) {
                cfg.menuX = std::clamp(winPos.x / dispW, 0.0f, 1.0f);
                cfg.menuY = std::clamp(winPos.y / dispH, 0.0f, 1.0f);
                expectedWindowPos_ = winPos;
                dirty = true;
            }

            if (programmaticSize) {
                lastAppliedCodeSize_ = winSize;
                if (cfg.menuSizeManual &&
                    (std::fabs(winSize.x - cfg.menuW * dispW) > 1.5f ||
                     std::fabs(winSize.y - cfg.menuH * dispH) > 1.5f)) {
                    cfg.menuW = std::clamp(winSize.x / dispW, 0.05f, 1.0f);
                    cfg.menuH = std::clamp(winSize.y / dispH, 0.05f, 1.0f);
                    dirty = true;
                }
            } else if (std::fabs(winSize.x - lastAppliedCodeSize_.x) > 1.5f ||
                       std::fabs(winSize.y - lastAppliedCodeSize_.y) > 1.5f) {
                cfg.menuSizeManual = true;
                cfg.menuW = std::clamp(winSize.x / dispW, 0.05f, 1.0f);
                cfg.menuH = std::clamp(winSize.y / dispH, 0.05f, 1.0f);
                lastAppliedCodeSize_ = winSize;
                dirty = true;
            }

            // Ensure window stays wholly inside screen after any size increase (manual or auto)
            const float fixX = std::clamp(winPos.x, 0.0f, (std::max)(0.0f, dispW - winSize.x));
            const float fixY = std::clamp(winPos.y, 0.0f, (std::max)(0.0f, dispH - winSize.y));
            if (std::fabs(fixX - winPos.x) > 0.5f || std::fabs(fixY - winPos.y) > 0.5f) {
                ImGui::SetWindowPos(ImVec2(fixX, fixY), ImGuiCond_Always);
                expectedWindowPos_ = ImVec2(fixX, fixY);
                if (cfg.menuX >= 0.0f || cfg.menuY >= 0.0f) {
                    cfg.menuX = std::clamp(fixX / dispW, 0.0f, 1.0f);
                    cfg.menuY = std::clamp(fixY / dispH, 0.0f, 1.0f);
                    dirty = true;
                }
            }
        }

        MenuLayout normalLayout(MeasureMode::Normal);
        const float innerAvailW = normalLayout.availWidth();
        const float innerAvailH = normalLayout.availHeight();
        const char* activeTabLogName = "General";
        bool switchedTabThisFrame = false;
        std::uint64_t hdrCondSig = 0ULL;
        std::uint64_t tabCondSig = 0ULL;
        std::uint64_t ftrCondSig = 0ULL;
        std::size_t drawnTabIdx  = 0u;

        ImGui::BeginGroup();
        normalLayout.resetCondTrack();
        renderChromeHeader(cfg, loc, normalLayout);
        hdrCondSig = normalLayout.condSignature();

        if (ImGui::BeginTabBar("##SvPlusTabs")) {
            auto switchTab = [&](int idx, const char* tabLabel) {
                if (activeTab_ != idx) {
                    switchedTabThisFrame = true;
                    if (activeTab_ == 2) {
                        auto& capture = audio::CaptureEngine::instance();
                        if (capture.isMicMonitorEnabled()) {
                            capture.setMicMonitorEnabled(false, "tab_changed");
                        }
                    }
                    activeTab_ = idx;
                    SVPLUS_LOG_INF_CAT("ui", "Switched config tab -> %s", tabLabel);
                }
            };
            for (std::size_t idx = 0; idx < tabs.size(); ++idx) {
                char tabBuf[128]{};
                const char* idPart = tabs[idx].id ? tabs[idx].id : "Tab";
                const char* lblPart = tabs[idx].label ? tabs[idx].label : "Tab";
                std::snprintf(tabBuf, sizeof(tabBuf), "%s###%s", lblPart, idPart);
                if (ImGui::BeginTabItem(tabBuf)) {
                    const char* logLabel =
                        (idx == 0) ? "General" :
                        (idx == 1) ? "Audio" :
                        (idx == 2) ? "Microphone" :
                        (idx == 3) ? "Players" : idPart;
                    switchTab(static_cast<int>(idx), logLabel);
                    activeTabLogName = logLabel;
                    drawnTabIdx = idx;
                    normalLayout.resetCondTrack();
                    if (tabs[idx].draw) {
                        tabs[idx].draw(normalLayout);
                    }
                    tabCondSig = normalLayout.condSignature();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        normalLayout.resetCondTrack();
        renderChromeFooter(cfg, loc, normalLayout, dirty);
        ftrCondSig = normalLayout.condSignature();
        ImGui::EndGroup();

        const std::uint64_t frameChromeCondSig =
            hdrCondSig ^ (ftrCondSig + 0x9e3779b97f4a7c15ULL + (hdrCondSig << 6) + (hdrCondSig >> 2));
        const bool condChangedThisFrame =
            (frameChromeCondSig != measured_.chromeCondSig) ||
            (drawnTabIdx < measured_.tabCondSigs.size() && tabCondSig != measured_.tabCondSigs[drawnTabIdx]);
        if (condChangedThisFrame) {
            pendingCondRemeasure_ = true;
        }

        const ImVec2 groupSz = ImGui::GetItemRectSize();
        const bool activelyResizing = isActivelyResizingWindow(ImGui::GetCurrentWindowRead());
        const bool sizeChangedFromPrev =
            std::fabs(winSize.x - prevObservedWinSize_.x) > 0.5f ||
            std::fabs(winSize.y - prevObservedWinSize_.y) > 0.5f;
        prevObservedWinSize_ = winSize;

        const bool langSwitchedMidFrame = (loc.activeLanguage() != lastMeasuredLang_);
        if (wasOpening || programmaticSize || langChanged || langSwitchedMidFrame ||
            extraChanged || extraRightChanged ||
            condRemeasure || switchedTabThisFrame || condChangedThisFrame ||
            activelyResizing || sizeChangedFromPrev) {
            settledSameSizeFrames_ = 0;
        } else {
            ++settledSameSizeFrames_;
        }

        const float overX = (std::max)(0.0f, groupSz.x - innerAvailW);
        const float overY = (std::max)(0.0f, groupSz.y - innerAvailH);
        if (!minExceedsScreen && !langSwitchedMidFrame && settledSameSizeFrames_ >= 2 && (overX > 0.5f || overY > 0.5f)) {
            const float addW = (overX > 0.5f) ? std::ceil(overX) : 0.0f;
            const float addH = (overY > 0.5f) ? std::ceil(overY) : 0.0f;
            const float nextW = (std::min)(maxConstraint.x, winSize.x + addW);
            const float nextH = (std::min)(maxConstraint.y, winSize.y + addH);
            if (nextW > winSize.x + 0.5f || nextH > winSize.y + 0.5f) {
                safetyBoostW_ += (std::max)(0.0f, nextW - winSize.x);
                safetyBoostH_ += (std::max)(0.0f, nextH - winSize.y);
                safetyNetForceSize_ = true;
                settledSameSizeFrames_ = 0;
                if (cfg.menuSizeManual) {
                    cfg.menuW = std::clamp(nextW / dispW, 0.05f, 1.0f);
                    cfg.menuH = std::clamp(nextH / dispH, 0.05f, 1.0f);
                    dirty = true;
                }
                const std::size_t langIdx = static_cast<std::size_t>(loc.activeLanguage());
                const std::size_t tabIdx  = static_cast<std::size_t>((std::clamp)(activeTab_, 0, 15));
                if (langIdx < Localization::kLanguageCount && !loggedOverflowWarn_[langIdx][tabIdx]) {
                    loggedOverflowWarn_[langIdx][tabIdx] = true;
                    SVPLUS_LOG_WRN_CAT(
                        "ui",
                        "layout overflow on tab %s: +%.0fx%.0f px; the measurement missed a case",
                        activeTabLogName,
                        addW,
                        addH);
                }
            }
        }
    }
    ImGui::End();

    anyPopupOpenLastFrame_ = (GImGui != nullptr && GImGui->OpenPopupStack.Size > 0);
    if (!keepOpen) {
        setOpen(false);
    }

    if (dirty) {
        outConfigDirty = true;
    }
}

void ConfigMenu::renderGeneralTabWithLayout(config::ClientConfig& cfg,
                                            Localization& loc,
                                            const UiTextures& textures,
                                            MenuLayout& layout,
                                            bool& dirty) {
    renderGeneralTab(cfg, loc, textures, layout, dirty);
}

void ConfigMenu::renderAudioTabWithLayout(config::ClientConfig& cfg,
                                          Localization& loc,
                                          MenuLayout& layout,
                                          bool& dirty) {
    renderAudioTab(cfg, loc, layout, dirty);
}

void ConfigMenu::renderGeneralTab(config::ClientConfig& cfg,
                                  Localization& loc,
                                  const UiTextures& textures,
                                  MenuLayout& layout,
                                  bool& dirty,
                                  const std::function<void(MenuLayout&)>& extraRight) {
    const auto& style = ImGui::GetStyle();
    const float uiAlpha = style.Alpha;
    const auto& voiceClient = net::VoiceClient::instance();
    const bool occlusionOn =
        voiceClient.isVoiceConnected() && voiceClient.isServerPlus() && voiceClient.isVoiceOcclusionEnabled();
    if (layout.visible(occlusionOn)) {
        ImGui::TextDisabled("%s", loc.tr(TextKey::VoiceOcclusionServerOn));
        ImGui::Spacing();
    }

    const float startLocalX = ImGui::GetCursorPosX();
    const float startLocalY = ImGui::GetCursorPosY();
    const float topScreenY  = ImGui::GetCursorScreenPos().y;
    const float winLeftScreenX = ImGui::GetCursorScreenPos().x - startLocalX;
    const float colGap      = style.ItemSpacing.x * 2.0f + 4.0f;
    const float resetBtnReserve =
        style.ItemSpacing.x + ImGui::CalcTextSize(loc.tr(TextKey::ResetButton)).x + style.FramePadding.x * 2.0f;
    const float knownRightMinBudget = (layout.measuring() && layout.passWidth() <= 0.0f)
        ? 0.0f
        : ((generalRightMinW_ > 0.0f ? generalRightMinW_ : lastGeneralRightW_) + colGap);
    float sepY1 = 0.0f;
    float sepY2 = 0.0f;

    // -------------------------------------------------------------------------
    // Left Column: Language, Hotkey, Micro HUD, Overhead 3D Icons
    // -------------------------------------------------------------------------
    ImGui::BeginGroup();
    {
        const auto langs = Localization::languages();
        const char* currentNative = Localization::nativeNameOf(loc.activeLanguage());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", loc.tr(TextKey::LanguageLabel));
        const float langLabelReserve =
            ImGui::CalcTextSize(loc.tr(TextKey::LanguageLabel)).x + 12.0f + style.ItemSpacing.x;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(layout.flexWidth(160.0f, 205.0f, knownRightMinBudget + langLabelReserve));
        if (ImGui::BeginCombo("##LangCombo", currentNative) && !layout.measuring()) {
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

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", loc.tr(TextKey::HotkeyLabel));
        ImGui::SameLine();
        const std::string keyLabel = capturingHotkey_
            ? std::string(loc.tr(TextKey::HotkeyPressAny))
            : config::formatVirtualKeyName(cfg.menuHotkey);
        const char* longestKeyLbl = layout.longestText(
            keyLabel.c_str(),
            {loc.tr(TextKey::HotkeyPressAny), "Mouse Mid", "CapsLock", "PageDown", "Backspace"});
        const float hotkeyBtnW = (std::max)(140.0f, ImGui::CalcTextSize(longestKeyLbl).x + style.FramePadding.x * 2.0f);
        if (ImGui::Button((keyLabel + "##MenuHotkeyBtn").c_str(), ImVec2(hotkeyBtnW, 0.0f)) && !layout.measuring()) {
            capturingHotkey_ = true;
            capturingDeafenHotkey_ = false;
            deafenHotkeyMenuConflict_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(loc.tr(TextKey::HotkeyResetDefault)) && !layout.measuring()) {
            capturingHotkey_ = false;
            if (cfg.menuHotkey != config::kDefaultMenuHotkey) {
                cfg.menuHotkey = config::kDefaultMenuHotkey;
                if (cfg.deafenHotkey == cfg.menuHotkey) {
                    cfg.deafenHotkey = 0;
                }
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset Hotkey -> default 0x%02X",
                               static_cast<unsigned>(config::kDefaultMenuHotkey));
        }

        sepY1 = layout.columnSeparator();

        constexpr float kMicroPreviewMaxPx = 42.0f * config::kMaxMicroIconScale;
        const float microSliderReserve = knownRightMinBudget + resetBtnReserve + 12.0f + kMicroPreviewMaxPx;
        ImGui::BeginGroup();
        if (ImGui::Checkbox(loc.tr(TextKey::ShowMicroIcon), &cfg.showMicroIcon) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showMicroIcon);
        ImGui::TextUnformatted(loc.tr(TextKey::MicroIconScale));
        if (sliderFloatWithArrows("##MicroIconScaleSlider", cfg.microIconScale,
                                  config::kMinMicroIconScale, config::kMaxMicroIconScale,
                                  0.01f, "%.2fx",
                                  layout.flexWidth(150.0f, 175.0f, microSliderReserve)) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetMicroScale").c_str()) && !layout.measuring()) {
            if (cfg.microIconScale != config::kDefaultMicroIconScale) {
                cfg.microIconScale = config::kDefaultMicroIconScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset MicroIconScale -> %.2f", cfg.microIconScale);
        }
        ImGui::EndDisabled();

        if (ImGui::Button(loc.tr(TextKey::ResetMicroPosition)) && !layout.measuring()) {
            cfg.microIconX = config::kDefaultMicroIconX;
            cfg.microIconY = config::kDefaultMicroIconY;
            HudOverlay::instance().requestResetMicroPosition();
            dirty = true;
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset MicroIconPosition");
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::BeginDisabled(!cfg.showMicroIcon);
        drawRealSizeIconPreview(
            reinterpret_cast<ImTextureID>(textures.microActive),
            42.0f * std::clamp(cfg.microIconScale, config::kMinMicroIconScale, config::kMaxMicroIconScale),
            kMicroPreviewMaxPx,
            uiAlpha,
            !layout.measuring());
        ImGui::EndDisabled();

        sepY2 = layout.columnSeparator();

        constexpr float kOverheadPreviewMaxPx = 28.0f * config::kMaxSpeakerIconScale;
        const float overheadSliderReserve = knownRightMinBudget + resetBtnReserve + 12.0f + kOverheadPreviewMaxPx;
        ImGui::BeginGroup();
        if (ImGui::Checkbox(loc.tr(TextKey::ShowOverheadIcons), &cfg.showOverheadIcons) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showOverheadIcons);

        ImGui::TextUnformatted(loc.tr(TextKey::OverheadIconBgOpacity));
        int opacityVal = std::clamp(cfg.overheadIconBgOpacity, 0, 100);
        if (sliderIntWithArrows("##OverheadOpacitySlider", opacityVal, 0, 100, 1, "%d%%",
                                layout.flexWidth(150.0f, 175.0f, overheadSliderReserve)) && !layout.measuring()) {
            if (opacityVal != cfg.overheadIconBgOpacity) {
                cfg.overheadIconBgOpacity = opacityVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetOverheadOpacity").c_str()) && !layout.measuring()) {
            if (cfg.overheadIconBgOpacity != config::kDefaultOverheadIconBgOpacity) {
                cfg.overheadIconBgOpacity = config::kDefaultOverheadIconBgOpacity;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset OverheadIconBgOpacity -> %d%%", cfg.overheadIconBgOpacity);
        }

        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerIconScale));
        if (sliderFloatWithArrows("##SpeakerIconScaleSlider", cfg.speakerIconScale,
                                  config::kMinSpeakerIconScale, config::kMaxSpeakerIconScale,
                                  0.01f, "%.2fx",
                                  layout.flexWidth(150.0f, 175.0f, overheadSliderReserve)) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetSpeakerIconScale").c_str()) && !layout.measuring()) {
            if (cfg.speakerIconScale != config::kDefaultSpeakerIconScale) {
                cfg.speakerIconScale = config::kDefaultSpeakerIconScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerIconScale -> %.2f", cfg.speakerIconScale);
        }

        ImGui::TextUnformatted(loc.tr(TextKey::OverheadIconHeight));
        int heightVal = std::clamp(cfg.overheadIconHeight, 0, 60);
        if (sliderIntWithArrows("##OverheadHeightSlider", heightVal, 0, 60, 1, "%d px",
                                layout.flexWidth(150.0f, 175.0f, overheadSliderReserve)) && !layout.measuring()) {
            if (heightVal != cfg.overheadIconHeight) {
                cfg.overheadIconHeight = heightVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetOverheadHeight").c_str()) && !layout.measuring()) {
            if (cfg.overheadIconHeight != config::kDefaultOverheadIconHeight) {
                cfg.overheadIconHeight = config::kDefaultOverheadIconHeight;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset OverheadIconHeight -> %d px", cfg.overheadIconHeight);
        }

        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::BeginGroup();
        ImGui::BeginDisabled(!cfg.showOverheadIcons);
        drawOverheadPreviewBox(
            reinterpret_cast<ImTextureID>(textures.speaker),
            cfg.overheadIconBgOpacity,
            uiAlpha,
            !layout.measuring());
        drawRealSizeIconPreview(
            reinterpret_cast<ImTextureID>(textures.speaker),
            28.0f * std::clamp(cfg.speakerIconScale, config::kMinSpeakerIconScale, config::kMaxSpeakerIconScale),
            kOverheadPreviewMaxPx,
            uiAlpha,
            !layout.measuring());
        ImGui::EndDisabled();
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
    const ImVec2 leftSize = ImGui::GetItemRectSize();

    constexpr float kMaxPreviewScale = config::kMaxSpeakerListScale;
    ImFont* listFnt = ImGui::GetFont();
    const float baseListFontPx = listFnt ? listFnt->FontSize : 17.0f;
    const char* sampleListName = "Player [0]";
    const float maxIconSz = 16.0f * kMaxPreviewScale;
    const float maxFontPx = baseListFontPx * kMaxPreviewScale;
    const ImVec2 maxTxtSz = listFnt
        ? listFnt->CalcTextSizeA(maxFontPx, FLT_MAX, 0.0f, sampleListName)
        : ImVec2(72.0f * kMaxPreviewScale, baseListFontPx * kMaxPreviewScale);
    const float maxBoxW = maxIconSz + 6.0f + maxTxtSz.x + 12.0f;
    const float maxBoxH = (std::max)(maxIconSz, maxTxtSz.y) + 8.0f;

    const bool unboundedWidth = (layout.measuring() && layout.passWidth() <= 0.0f);
    const float minRightStart = leftSize.x + colGap;
    const float measuredRightW = [&]() noexcept {
        if (layout.measuring()) {
            return (layout.mode() == MeasureMode::MeasurePreferred)
                ? generalRightPrefW_
                : generalRightMinW_;
        }
        if (generalRightPrefW_ > 0.0f && minRightStart + generalRightPrefW_ <= layout.availWidth() + 0.5f) {
            return generalRightPrefW_;
        }
        if (generalRightMinW_ > 0.0f) {
            return generalRightMinW_;
        }
        return lastGeneralRightW_;
    }();
    const float slack = (std::max)(0.0f, layout.availWidth() - (minRightStart + measuredRightW));
    const float idealMid = (layout.availWidth() + colGap) * 0.5f;
    const float rightStartOffset = unboundedWidth
        ? minRightStart
        : std::clamp(idealMid, minRightStart, minRightStart + slack);

    // -------------------------------------------------------------------------
    // Right Column: Speaker List HUD Controls & Fixed-Max Preview Box
    // -------------------------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(startLocalX + rightStartOffset, startLocalY));
    ImGui::BeginGroup();
    {
        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerList), &cfg.showSpeakerList) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::BeginDisabled(!cfg.showSpeakerList);

        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerListHeader), &cfg.speakerListHeader) && !layout.measuring()) {
            dirty = true;
        }
        if (ImGui::Checkbox(loc.tr(TextKey::ShowSpeakerListBg), &cfg.speakerListBg) && !layout.measuring()) {
            dirty = true;
        }

        ImGui::BeginDisabled(!cfg.speakerListBg);
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerListBgOpacity));
        int bgOpacityVal = std::clamp(cfg.speakerListBgOpacity, 0, 100);
        if (sliderIntWithArrows("##ListBgOpacitySlider", bgOpacityVal, 0, 100, 1, "%d%%",
                                layout.flexWidth(160.0f, 205.0f, resetBtnReserve)) && !layout.measuring()) {
            if (bgOpacityVal != cfg.speakerListBgOpacity) {
                cfg.speakerListBgOpacity = bgOpacityVal;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListBgOpacity").c_str()) && !layout.measuring()) {
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
        if (sliderFloatWithArrows("##ListScaleSlider", cfg.speakerListScale,
                                  config::kMinSpeakerListScale, config::kMaxSpeakerListScale,
                                  0.01f, "%.2fx",
                                  layout.flexWidth(160.0f, 205.0f, resetBtnReserve + 12.0f + maxBoxW)) && !layout.measuring()) {
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListScale").c_str()) && !layout.measuring()) {
            if (cfg.speakerListScale != config::kDefaultSpeakerListScale) {
                cfg.speakerListScale = config::kDefaultSpeakerListScale;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListScale -> %.2f", cfg.speakerListScale);
        }
        ImGui::EndGroup();

        // Fixed max-scale reservation box so dragging SpeakerListScale never shifts layout
        ImGui::SameLine(0.0f, 12.0f);
        {
            ImGui::Dummy(ImVec2(maxBoxW, maxBoxH));

            if (!layout.measuring()) {
                const ImVec2 bMin = ImGui::GetItemRectMin();
                const ImVec2 bMax = ImGui::GetItemRectMax();
                const ImVec2 boxCenter((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);

                const float sc     = std::clamp(cfg.speakerListScale, config::kMinSpeakerListScale, config::kMaxSpeakerListScale);
                const float iconSz = 16.0f * sc;
                const float fontSz = baseListFontPx * sc;
                const ImVec2 txtSz = listFnt
                    ? listFnt->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, sampleListName)
                    : ImVec2(72.0f * sc, 17.0f * sc);
                const float rowW = iconSz + 6.0f + txtSz.x + 12.0f;
                const float rowH = (std::max)(iconSz, txtSz.y) + 8.0f;

                const ImVec2 pMin(boxCenter.x - rowW * 0.5f, boxCenter.y - rowH * 0.5f);
                const ImVec2 pMax(boxCenter.x + rowW * 0.5f, boxCenter.y + rowH * 0.5f);

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
                    dl->AddText(listFnt, fontSz, ImVec2(tx + d[0] * o, ty + d[1] * o), outCol, sampleListName);
                }
                dl->AddText(listFnt, fontSz, ImVec2(tx, ty), IM_COL32(255, 255, 255, fgA), sampleListName);
            }
        }

        ImGui::Spacing();
        ImGui::TextUnformatted(loc.tr(TextKey::SpeakerListMax));
        if (!layout.measuring() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", loc.tr(TextKey::SpeakerListMaxTooltip));
        }
        int maxVal = std::clamp(cfg.speakerListMax, 1, 15);
        SliderArrowResult maxListRes{};
        if (sliderIntWithArrows("##ListMaxSlider", maxVal, 1, 15, 1, "%d",
                                layout.flexWidth(160.0f, 205.0f, resetBtnReserve),
                                nullptr, &maxListRes) && !layout.measuring()) {
            cfg.speakerListMax = std::clamp(maxVal, 1, 15);
            dirty = true;
        }
        if (!layout.measuring() && maxListRes.anyHovered) {
            ImGui::SetTooltip("%s", loc.tr(TextKey::SpeakerListMaxTooltip));
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetListMax").c_str()) && !layout.measuring()) {
            if (cfg.speakerListMax != config::kDefaultSpeakerListMax) {
                cfg.speakerListMax = config::kDefaultSpeakerListMax;
                dirty = true;
            }
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListMax -> %d", cfg.speakerListMax);
        }

        ImGui::Spacing();
        if (ImGui::Button(loc.tr(TextKey::ResetSpeakerListPosition)) && !layout.measuring()) {
            cfg.speakerListX = config::kDefaultSpeakerListX;
            cfg.speakerListY = config::kDefaultSpeakerListY;
            HudOverlay::instance().requestResetSpeakerListPosition();
            dirty = true;
            SVPLUS_LOG_INF_CAT("ui", "Clicked Reset SpeakerListPosition");
        }

        ImGui::EndDisabled();
        if (extraRight) {
            extraRight(layout);
        }
    }
    ImGui::EndGroup();
    const ImVec2 rightSize = ImGui::GetItemRectSize();
    if (unboundedWidth) {
        if (layout.mode() == MeasureMode::MeasureMin) {
            generalRightMinW_ = rightSize.x;
        } else {
            generalRightPrefW_ = rightSize.x;
        }
    } else if (!layout.measuring()) {
        lastGeneralRightW_ = rightSize.x;
    }

    const float colMaxH = (std::max)(leftSize.y, rightSize.y);
    if (!layout.measuring()) {
        const float divScreenX = winLeftScreenX + startLocalX + rightStartOffset - colGap * 0.5f;
        const ImU32 sepCol = ImGui::GetColorU32(ImGuiCol_Separator);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(divScreenX, topScreenY),
            ImVec2(divScreenX, topScreenY + colMaxH),
            sepCol,
            1.0f);
        const float sepStartX = winLeftScreenX + startLocalX;
        const float sepWidth  = (std::max)(0.0f, divScreenX - sepStartX);
        layout.drawColumnSeparatorLine(sepStartX, sepY1, sepWidth);
        layout.drawColumnSeparatorLine(sepStartX, sepY2, sepWidth);
    }

    ImGui::SetCursorPos(ImVec2(startLocalX, startLocalY + colMaxH));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void ConfigMenu::renderAudioTab(config::ClientConfig& cfg,
                                Localization& loc,
                                MenuLayout& layout,
                                bool& dirty) {
    ImGui::Spacing();
    const auto& style = ImGui::GetStyle();
    if (ImGui::Checkbox(loc.tr(TextKey::SoundEnable), &cfg.soundEnable) && !layout.measuring()) {
        dirty = true;
    }

    const char* masterLbl = loc.tr(TextKey::SoundVolume);
    const float masterReserve =
        (masterLbl && masterLbl[0] != '\0') ? (style.ItemInnerSpacing.x + ImGui::CalcTextSize(masterLbl).x) : 0.0f;
    if (sliderIntWithArrows("##SoundVolume", cfg.soundVolume, 0, 100, 1, "%d%%",
                            layout.flexWidth(140.0f, 280.0f, masterReserve), masterLbl) && !layout.measuring()) {
        dirty = true;
    }

    const char* resetLbl = loc.tr(TextKey::ResetButton);
    const float resetBtnReserve = style.ItemSpacing.x + ImGui::CalcTextSize(resetLbl).x + style.FramePadding.x * 2.0f;

    ImGui::Spacing();
    ImGui::TextUnformatted(loc.tr(TextKey::LocalVolumeLabel));
    if (sliderIntWithArrows("##LocalVolume", cfg.localVolume, 0, config::kMaxChannelVolume, 1, "%d%%",
                            layout.flexWidth(140.0f, 280.0f, resetBtnReserve)) && !layout.measuring()) {
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(resetLbl) + "##ResetLocalVolume").c_str()) && !layout.measuring()) {
        if (cfg.localVolume != config::kDefaultLocalVolume) {
            cfg.localVolume = config::kDefaultLocalVolume;
            dirty = true;
        }
    }

    ImGui::TextUnformatted(loc.tr(TextKey::GlobalVolumeLabel));
    if (sliderIntWithArrows("##GlobalVolume", cfg.globalVolume, 0, config::kMaxChannelVolume, 1, "%d%%",
                            layout.flexWidth(140.0f, 280.0f, resetBtnReserve)) && !layout.measuring()) {
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(resetLbl) + "##ResetGlobalVolume").c_str()) && !layout.measuring()) {
        if (cfg.globalVolume != config::kDefaultGlobalVolume) {
            cfg.globalVolume = config::kDefaultGlobalVolume;
            dirty = true;
        }
    }

    ImGui::Spacing();
    if (ImGui::Checkbox(loc.tr(TextKey::SoundBalancer), &cfg.soundBalancer) && !layout.measuring()) {
        dirty = true;
    }
    if (ImGui::Checkbox(loc.tr(TextKey::SoundFilter), &cfg.soundFilter) && !layout.measuring()) {
        dirty = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    auto& playback = audio::PlaybackEngine::instance();
    bool deafened = playback.isDeafened();
    if (ImGui::Checkbox(loc.tr(TextKey::DeafenEnable), &deafened) && !layout.measuring()) {
        playback.setDeafened(deafened, "menu");
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", loc.tr(TextKey::DeafenHotkeyLabel));
    ImGui::SameLine();
    const std::string deafenKeyLabel = capturingDeafenHotkey_
        ? std::string(loc.tr(TextKey::HotkeyPressAny))
        : (cfg.deafenHotkey == 0
               ? std::string(loc.tr(TextKey::DeafenHotkeyNone))
               : config::formatVirtualKeyName(cfg.deafenHotkey));
    const char* longestDeafenKeyLbl = layout.longestText(
        deafenKeyLabel.c_str(),
        {loc.tr(TextKey::HotkeyPressAny), loc.tr(TextKey::DeafenHotkeyNone),
         "Mouse Mid", "CapsLock", "PageDown", "Backspace"});
    const float deafenBtnW =
        (std::max)(160.0f, ImGui::CalcTextSize(longestDeafenKeyLbl).x + style.FramePadding.x * 2.0f);
    if (ImGui::Button((deafenKeyLabel + "##DeafenHotkeyBtn").c_str(), ImVec2(deafenBtnW, 0.0f)) && !layout.measuring()) {
        capturingDeafenHotkey_ = true;
        capturingHotkey_ = false;
        deafenHotkeyMenuConflict_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(loc.tr(TextKey::DeafenHotkeyClear)) + "##DeafenHotkeyClear").c_str()) && !layout.measuring()) {
        capturingDeafenHotkey_ = false;
        deafenHotkeyMenuConflict_ = false;
        if (cfg.deafenHotkey != 0) {
            cfg.deafenHotkey = 0;
            dirty = true;
        }
    }
    if (layout.visible(deafenHotkeyMenuConflict_)) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", loc.tr(TextKey::DeafenHotkeyMenuConflict));
    }
    const bool hasVoiceConflict =
        cfg.deafenHotkey != 0 && cfg.deafenHotkey <= 0xFEu &&
        audio::CaptureEngine::instance().hasActivationKey(static_cast<std::uint8_t>(cfg.deafenHotkey));
    if (layout.visible(hasVoiceConflict)) {
        const std::string actualVk = config::formatVirtualKeyName(hasVoiceConflict ? cfg.deafenHotkey : 0x7Au);
        const char* vkText = layout.longestText(
            actualVk.c_str(),
            {"Mouse Mid", "CapsLock", "PageDown", "Backspace", "Key 0xFF"});
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            loc.tr(TextKey::DeafenHotkeyVoiceConflict),
            vkText);
    }
}

void ConfigMenu::renderMicrophoneTab(config::ClientConfig& cfg,
                                     Localization& loc,
                                     MenuLayout& layout,
                                     bool& dirty) {
    const auto& voiceClient = net::VoiceClient::instance();
    const bool toggleAllowed = !voiceClient.isVoiceConnected() || !voiceClient.isServerPlus() || voiceClient.isAllowToggleMode();
    renderMicrophoneTabWithLayout(cfg, loc, toggleAllowed, layout, dirty);
}

void ConfigMenu::renderMicrophoneTabWithState(config::ClientConfig& cfg,
                                              Localization& loc,
                                              bool toggleAllowed,
                                              bool& dirty) {
    MenuLayout normalLayout(MeasureMode::Normal);
    renderMicrophoneTabWithLayout(cfg, loc, toggleAllowed, normalLayout, dirty);
}

void ConfigMenu::renderMicrophoneTabWithLayout(config::ClientConfig& cfg,
                                               Localization& loc,
                                               bool toggleAllowed,
                                               MenuLayout& layout,
                                               bool& dirty) {
    ImGui::Spacing();
    auto& capture = audio::CaptureEngine::instance();

    if (ImGui::Checkbox(loc.tr(TextKey::MicroEnable), &cfg.microEnable) && !layout.measuring()) {
        dirty = true;
    }

    const auto& style = ImGui::GetStyle();
    auto fitRowItemWidth = [&](const char* rightLabel, float extraRightW) noexcept -> float {
        const float lblW = (rightLabel && rightLabel[0] != '\0')
            ? (style.ItemInnerSpacing.x + ImGui::CalcTextSize(rightLabel).x)
            : 0.0f;
        return layout.flexWidth(140.0f, 300.0f, lblW + extraRightW);
    };

    const char* talkModeLabels[2] = {
        loc.tr(TextKey::TalkModePush),
        loc.tr(TextKey::TalkModeToggle)
    };
    const int effectiveModeIdx = (toggleAllowed && cfg.talkMode == config::TalkMode::Toggle) ? 1 : 0;
    ImGui::SetNextItemWidth(fitRowItemWidth(loc.tr(TextKey::TalkModeLabel), 0.0f));
    if (ImGui::BeginCombo(loc.tr(TextKey::TalkModeLabel), talkModeLabels[effectiveModeIdx]) && !layout.measuring()) {
        const bool pttSelected = (effectiveModeIdx == 0);
        if (ImGui::Selectable(talkModeLabels[0], pttSelected) && toggleAllowed) {
            cfg.talkMode = config::TalkMode::PushToTalk;
            dirty = true;
        }
        if (pttSelected) {
            ImGui::SetItemDefaultFocus();
        }

        const bool toggleSelected = (effectiveModeIdx == 1);
        ImGui::BeginDisabled(!toggleAllowed);
        if (ImGui::Selectable(talkModeLabels[1], toggleSelected) && toggleAllowed) {
            cfg.talkMode = config::TalkMode::Toggle;
            dirty = true;
        }
        if (toggleSelected) {
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndDisabled();
        if (!toggleAllowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", loc.tr(TextKey::TalkModeDisabledByServerTip));
        }
        ImGui::EndCombo();
    }
    if (!layout.measuring() && !toggleAllowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", loc.tr(TextKey::TalkModeDisabledByServerTip));
    }

    if (!layout.measuring()) {
        static double s_lastUiDevRefresh = 0.0;
        const double uiNow = ImGui::GetTime();
        if (uiNow - s_lastUiDevRefresh >= 1.5) {
            s_lastUiDevRefresh = uiNow;
            capture.requestRefreshDevices();
        }
    }

    decltype(capture.devicesSnapshot()) devices{};
    if (!layout.measuring()) {
        devices = capture.devicesSnapshot();
    }
    std::string fullDevName = cfg.microDevice;
    if (fullDevName.empty()) {
        fullDevName = loc.tr(TextKey::MicroDeviceDefault);
    }

    const char* resetDevLabel = loc.tr(TextKey::MicroResetDevice);
    const float resetDevBtnW =
        style.ItemSpacing.x + ImGui::CalcTextSize(resetDevLabel).x + style.FramePadding.x * 2.0f;
    const float devComboW = fitRowItemWidth(loc.tr(TextKey::MicroDevice), resetDevBtnW);
    const float devPreviewMaxW = (std::max)(40.0f, devComboW - ImGui::GetFrameHeight() - style.FramePadding.x * 2.0f);
    const std::string fittedDevName = fitUtf8TextToWidth(fullDevName, devPreviewMaxW);

    ImGui::SetNextItemWidth(devComboW);
    if (ImGui::BeginCombo(loc.tr(TextKey::MicroDevice), fittedDevName.c_str()) && !layout.measuring()) {
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
    if (!layout.measuring() && fittedDevName != fullDevName && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", fullDevName.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(resetDevLabel) && !layout.measuring()) {
        capture.requestRefreshDevices();
        capture.requestSelectDevice(cfg.microDevice);
    }

    const char* resetGainLbl = loc.tr(TextKey::ResetButton);
    const float resetGainBtnW =
        style.ItemSpacing.x + ImGui::CalcTextSize(resetGainLbl).x + style.FramePadding.x * 2.0f;
    const float gainW = fitRowItemWidth(loc.tr(TextKey::MicroVolume), resetGainBtnW);
    if (sliderIntWithArrows("##MicroGain", cfg.microGain, 0, config::kMaxMicroGain, 1, "%d%%", gainW,
                            loc.tr(TextKey::MicroVolume)) && !layout.measuring()) {
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(resetGainLbl) + "##ResetMicroGain").c_str()) && !layout.measuring()) {
        if (cfg.microGain != config::kDefaultMicroGain) {
            cfg.microGain = config::kDefaultMicroGain;
            dirty = true;
        }
    }
    if (ImGui::Checkbox(loc.tr(TextKey::MicroNoiseSuppress), &cfg.microNoiseSuppress) && !layout.measuring()) {
        dirty = true;
    }

    ImGui::Spacing();
    ImGui::Text("%s:", loc.tr(TextKey::MicroTestLevel));
    const float meterW = layout.flexWidth(140.0f, 300.0f, 0.0f);
    ImGui::SetNextItemWidth(meterW);
    ImGui::ProgressBar(layout.measuring() ? 0.0f : capture.currentInputLevel(), ImVec2(meterW, 18.0f));

    ImGui::Spacing();
    const bool micReadyForMonitor = cfg.microEnable && capture.hasMicrophone();
    if (!layout.measuring() && !micReadyForMonitor && capture.isMicMonitorEnabled()) {
        capture.setMicMonitorEnabled(false, !cfg.microEnable ? "micro_disabled" : "no_mic_device");
    }
    const bool monitoring = !layout.measuring() && capture.isMicMonitorEnabled();
    ImGui::BeginDisabled(!micReadyForMonitor && !monitoring);
    const char* listenLblActual = monitoring ? loc.tr(TextKey::MicroListenStop) : loc.tr(TextKey::MicroListenStart);
    const char* listenLblForSize = layout.longestText(
        listenLblActual, {loc.tr(TextKey::MicroListenStart), loc.tr(TextKey::MicroListenStop)});
    const float listenBtnW = ImGui::CalcTextSize(listenLblForSize).x + style.FramePadding.x * 2.0f;
    if (monitoring) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.22f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.76f, 0.28f, 0.28f, 1.00f));
    }
    if (ImGui::Button(listenLblActual, ImVec2(listenBtnW, 0.0f)) && !layout.measuring()) {
        capture.setMicMonitorEnabled(!monitoring, "user");
    }
    if (monitoring) {
        ImGui::PopStyleColor(2);
    }
    ImGui::EndDisabled();
    if (!layout.measuring() && !micReadyForMonitor && !monitoring && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", loc.tr(TextKey::MicroListenDisabledTip));
    }

    layout.sameLineWrappedText(loc.tr(TextKey::MicroListenEchoTip), 160.0f, true);
}

void ConfigMenu::renderBlacklistTabWithState(config::ClientConfig& cfg,
                                             Localization& loc,
                                             const BlacklistTabView& view,
                                             bool& dirty) {
    MenuLayout normalLayout(MeasureMode::Normal);
    renderBlacklistTabImpl(cfg, loc, view, nullptr, normalLayout, dirty);
}

void ConfigMenu::renderBlacklistTabWithLayout(config::ClientConfig& cfg,
                                              Localization& loc,
                                              const BlacklistTabView& view,
                                              MenuLayout& layout,
                                              bool& dirty) {
    renderBlacklistTabImpl(cfg, loc, view, nullptr, layout, dirty);
}

void ConfigMenu::renderBlacklistTabImpl(config::ClientConfig& cfg,
                                          Localization& loc,
                                          const BlacklistTabView& view,
                                          const std::vector<std::size_t>* precomputedNearbyOrder,
                                          MenuLayout& layout,
                                          bool& dirty) {
    const auto& style = ImGui::GetStyle();
    const float btnPadX = style.FramePadding.x * 2.0f;
    auto calcBtnWidth = [&](const char* label) {
        return ImGui::CalcTextSize(label).x + btnPadX;
    };

    constexpr float kWeightSideBySidePart1     = 0.43f;
    constexpr float kWeightSideBySidePart2     = 0.57f;
    constexpr float kWeightStackedOnline       = 0.35f;
    constexpr float kWeightStackedActiveMuted  = 0.25f;
    constexpr float kWeightStackedAllBlacklist = 0.40f;
    constexpr float kPrefPart1ExtraPerCol      = 2.0f;

    const float footerBtnH    = ImGui::GetFrameHeight();
    const float bottomReserve = footerBtnH + style.ItemSpacing.y * 2.0f;
    const float availW        = layout.availWidth();

    auto drawHeaderWithRightControl = [&](const std::string& titleText,
                                          float rightWidth,
                                          const ImVec4& col,
                                          float maxWidth) {
        const float maxTextW = (std::max)(80.0f, maxWidth - rightWidth - style.ItemSpacing.x);
        ImGui::AlignTextToFramePadding();
        fitUtf8TextToWidthInto(titleText, maxTextW, fittedNameScratch_);
        ImGui::TextColored(col, "%s", fittedNameScratch_.c_str());
        if (!layout.measuring() && fittedNameScratch_ != titleText && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", titleText.c_str());
        }
    };

    if (!layout.measuring() && !view.nearbyListEnabled) {
        nearbyOnlyFilter_ = false;
    }

    const bool connectedView = layout.measuring() ? true : view.connected;
    const bool showFilterBtns = layout.measuring() ? true : view.nearbyListEnabled;

    // -------------------------------------------------------------------------
    // Precompute exact content-driven minimums for Part 1 & Part 2
    // -------------------------------------------------------------------------
    constexpr float kMinPlayerNameW  = 120.0f;
    constexpr float kStatusIconPx    = 16.0f;
    constexpr float kPrefPlayerNameW = kMinPlayerNameW;

    const char* muteHereTxt   = loc.tr(TextKey::BlacklistMuteHereBtn);
    const char* muteAllTxt    = loc.tr(TextKey::BlacklistMuteAllBtn);
    const char* unmuteTxt     = loc.tr(TextKey::BlacklistUnmuteButton);
    const char* unmuteHereTxt = loc.tr(TextKey::BlacklistUnmuteHereButton);
    const float pairBtnW      = calcBtnWidth(muteHereTxt) + calcBtnWidth(muteAllTxt) + style.ItemInnerSpacing.x;
    const float maxActionBtnW = (std::max)({pairBtnW, calcBtnWidth(unmuteTxt), calcBtnWidth(unmuteHereTxt)});
    const float vol200W       = calcBtnWidth("200%");
    const float maxBadgeTextW = (std::max)(
        ImGui::CalcTextSize(loc.tr(TextKey::VoiceStatusNoMicBadge)).x,
        ImGui::CalcTextSize(loc.tr(TextKey::VoiceStatusNoHearBadge)).x);
    const float maxBadgeW     = kStatusIconPx + 4.0f + maxBadgeTextW + style.ItemSpacing.x;
    const float childChromeW  = style.WindowPadding.x * 2.0f + style.ScrollbarSize + 4.0f;

    const char* titleOnlineTxt  = loc.tr(TextKey::BlacklistPlayersOnline);
    const char* allFilterTxt    = loc.tr(TextKey::BlacklistFilterAll);
    const char* nearbyFilterTxt = loc.tr(TextKey::BlacklistFilterNearby);
    const float filterBtnsW     = showFilterBtns
        ? (calcBtnWidth(allFilterTxt) + calcBtnWidth(nearbyFilterTxt) + style.ItemInnerSpacing.x)
        : 0.0f;
    const float leftHeaderMinW = ImGui::CalcTextSize(titleOnlineTxt).x +
        (showFilterBtns ? (style.ItemSpacing.x + filterBtnsW) : 0.0f) + childChromeW;
    const float leftRowMinW = kMinPlayerNameW + maxBadgeW + style.ItemSpacing.x +
        vol200W + style.ItemInnerSpacing.x + maxActionBtnW + childChromeW;
    const float leftMinW  = (std::max)(leftHeaderMinW, leftRowMinW);
    const float leftPrefW = (std::max)(leftHeaderMinW, leftRowMinW - kMinPlayerNameW + kPrefPlayerNameW) +
        kPrefPart1ExtraPerCol;

    const char* rmTxt  = loc.tr(TextKey::BlacklistRemoveBtn);
    const char* clrTxt = loc.tr(TextKey::BlacklistClearAll);
    const float rmW    = calcBtnWidth(rmTxt);
    const float clrW   = calcBtnWidth(clrTxt);
    const char* scopeAllTxt = loc.tr(TextKey::BlacklistScopeAllLabel);
    const char* scopeSrvTxt = loc.tr(TextKey::BlacklistScopeServerLabel);
    char scopeTagSample[64]{};
    const char* widestScope =
        (ImGui::CalcTextSize(scopeAllTxt).x >= ImGui::CalcTextSize(scopeSrvTxt).x) ? scopeAllTxt : scopeSrvTxt;
    std::snprintf(scopeTagSample, sizeof(scopeTagSample), "[%s]", widestScope);
    const float scopeTagW = ImGui::CalcTextSize(scopeTagSample).x;

    char rightHdrSample[96]{};
    std::snprintf(rightHdrSample, sizeof(rightHdrSample), "%s (999)", loc.tr(TextKey::BlacklistMutedList));
    const float rightHeaderMinW = ImGui::CalcTextSize(rightHdrSample).x + style.ItemSpacing.x + clrW + childChromeW;
    const float rightRowMinW    = kMinPlayerNameW + style.ItemSpacing.x + scopeTagW + style.ItemSpacing.x + rmW + childChromeW;
    const float rightMinW       = (std::max)(rightHeaderMinW, rightRowMinW);
    const float rightPrefW      = (std::max)(rightHeaderMinW, rightRowMinW - kMinPlayerNameW + kPrefPlayerNameW) +
        kPrefPart1ExtraPerCol;

    const char* originBtnTxt = loc.tr(TextKey::BlacklistScopeOriginBtn);
    const float actionsColW  = (std::max)(
        calcBtnWidth(originBtnTxt) + calcBtnWidth(muteAllTxt) + calcBtnWidth(rmTxt) +
            style.ItemInnerSpacing.x * 2.0f + style.CellPadding.x * 2.0f + 4.0f,
        ImGui::CalcTextSize(loc.tr(TextKey::BlacklistColActions)).x + style.CellPadding.x * 2.0f + 4.0f);
    const float scopeColW    = (std::max)({
        128.0f,
        ImGui::CalcTextSize(loc.tr(TextKey::BlacklistColScope)).x + style.CellPadding.x * 2.0f + 6.0f,
        (std::max)(ImGui::CalcTextSize(scopeAllTxt).x, ImGui::CalcTextSize(scopeSrvTxt).x) + style.CellPadding.x * 2.0f + 6.0f});
    const float epColW       = (std::max)(
        134.0f,
        ImGui::CalcTextSize(loc.tr(TextKey::BlacklistColEndpoint)).x + style.CellPadding.x * 2.0f + 6.0f);
    const float part2MinW    =
        110.0f + 130.0f + epColW + scopeColW + actionsColW +
        style.CellPadding.x * 8.0f + style.ScrollbarSize + 12.0f;
    const float part2PrefW   = part2MinW;

    const float textLineH       = ImGui::GetTextLineHeightWithSpacing();
    const float listHdrExtraPx  = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + style.WindowPadding.y * 2.0f + 4.0f;
    const float part2HdrExtraPx = ImGui::GetFrameHeight() + style.ItemSpacing.y + style.CellPadding.y * 2.0f + 6.0f;
    const float minPart1RowH    = std::ceil(kListMinRowsPart1 * textLineH + listHdrExtraPx);
    const float prefPart1RowH   = std::ceil(kListPrefRowsPart1 * textLineH + listHdrExtraPx);
    const float minPart2H       = std::ceil(kListMinRowsAllBlacklist * textLineH + part2HdrExtraPx);
    const float prefPart2H      = std::ceil(kListPrefRowsAllBlacklist * textLineH + part2HdrExtraPx);

    float part2H = minPart2H;

    // =========================================================================
    // Part 1: Current server
    // =========================================================================
    if (!connectedView) {
        ImGui::TextColored(ImVec4(0.86f, 0.90f, 0.96f, 1.0f), "%s", loc.tr(TextKey::BlacklistCurrentServerHeader));
        const float boxW = layout.fillWidth((std::max)(leftMinW, part2MinW), leftPrefW + style.ItemSpacing.x + rightPrefW);
        const float boxH = ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f + 4.0f;
        if (layout.beginListChild("##Part1Disconnected", boxW, boxH, true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistDisconnected));
        }
        layout.endListChild();
    } else {
        std::string srvTitle = loc.tr(TextKey::BlacklistCurrentServerHeader);
        if (!view.serverName.empty() && !view.serverEndpoint.empty()) {
            srvTitle += ": " + view.serverName + " (" + view.serverEndpoint + ")";
        } else if (!view.serverEndpoint.empty()) {
            srvTitle += ": " + view.serverEndpoint;
        }

        const float minSearchHdrW = ImGui::CalcTextSize(loc.tr(TextKey::BlacklistCurrentServerHeader)).x + 120.0f + style.ItemSpacing.x;
        const float searchW = layout.flexWidth(140.0f, 260.0f, ImGui::CalcTextSize(loc.tr(TextKey::BlacklistCurrentServerHeader)).x + style.ItemSpacing.x + 20.0f);
        const float part1WidthBound = (layout.measuring() && layout.passWidth() <= 0.0f)
            ? (layout.mode() == MeasureMode::MeasureMin
                   ? (std::max)({minSearchHdrW, leftMinW, rightMinW})
                   : (std::max)(minSearchHdrW, leftPrefW + style.ItemSpacing.x + rightPrefW))
            : availW;

        drawHeaderWithRightControl(srvTitle, searchW, ImVec4(0.86f, 0.90f, 0.96f, 1.0f), part1WidthBound);
        placeRightOnSameLine(part1WidthBound, searchW);
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##BlacklistSearch", loc.tr(TextKey::BlacklistSearchPlaceholder),
                                 searchBuf_, sizeof(searchBuf_));

        const float stackThresholdW = std::ceil(leftMinW + rightMinW + style.ItemSpacing.x);
        const bool stackPart1 = (layout.measuring() && layout.passWidth() <= 0.0f)
            ? (layout.mode() == MeasureMode::MeasureMin)
            : (availW + 0.5f < stackThresholdW);

        float leftW  = 0.0f;
        float rightW = 0.0f;
        if (layout.measuring() && layout.passWidth() <= 0.0f) {
            if (stackPart1) {
                leftW  = (std::max)(leftMinW, rightMinW);
                rightW = leftW;
            } else {
                leftW  = leftPrefW;
                rightW = rightPrefW;
            }
        } else if (stackPart1) {
            leftW  = (std::max)(leftMinW, availW);
            rightW = (std::max)(rightMinW, availW);
        } else {
            const float usableW = availW - style.ItemSpacing.x;
            leftW  = std::clamp(usableW * 0.60f, leftMinW, (std::max)(leftMinW, usableW - rightMinW));
            rightW = (std::max)(rightMinW, usableW - leftW);
        }

        const float part2HeaderH = ImGui::GetFrameHeight();
        const float sideBySideNonListH = style.ItemSpacing.y * 3.0f + part2HeaderH + bottomReserve;
        const float stackedNonListH    = style.ItemSpacing.y * 4.0f + part2HeaderH + bottomReserve;

        float leftH  = minPart1RowH;
        float rightH = minPart1RowH;
        if (stackPart1) {
            const std::array<float, 3> mins{minPart1RowH, minPart1RowH, minPart2H};
            const std::array<float, 3> weights{
                kWeightStackedOnline, kWeightStackedActiveMuted, kWeightStackedAllBlacklist};
            std::array<float, 3> heights{};
            const float budget = layout.measuring()
                ? 0.0f
                : (std::max)(0.0f, layout.availHeight() - stackedNonListH);
            layout.distributeHeights(mins, weights, budget, heights);
            leftH  = heights[0];
            rightH = heights[1];
            part2H = heights[2];
        } else {
            const std::array<float, 2> mins{minPart1RowH, minPart2H};
            const std::array<float, 2> weights{kWeightSideBySidePart1, kWeightSideBySidePart2};
            std::array<float, 2> heights{};
            float budget = 0.0f;
            if (layout.measuring()) {
                if (layout.mode() == MeasureMode::MeasurePreferred) {
                    const float prefSurplus = (std::max)(
                        (prefPart1RowH - minPart1RowH) / kWeightSideBySidePart1,
                        (prefPart2H - minPart2H) / kWeightSideBySidePart2);
                    budget = minPart1RowH + minPart2H + prefSurplus;
                }
            } else {
                budget = (std::max)(0.0f, layout.availHeight() - sideBySideNonListH);
            }
            layout.distributeHeights(mins, weights, budget, heights);
            leftH  = heights[0];
            rightH = heights[0];
            part2H = heights[1];
        }
        const std::string_view filter(searchBuf_);

        // Left child: Online / Nearby players on current server
        if (layout.beginListChild("##OnlinePlayers", leftW, leftH, true)) {
            const float hdrStartX = ImGui::GetCursorPosX();
            const float hdrMaxX   = hdrStartX + leftW - style.WindowPadding.x * 2.0f -
                                    (layout.measuring() ? style.ScrollbarSize : 0.0f);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(titleOnlineTxt);
            if (showFilterBtns) {
                const float titleEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
                if (titleEndX + style.ItemSpacing.x + filterBtnsW <= hdrMaxX) {
                    ImGui::SameLine((std::max)(titleEndX + style.ItemSpacing.x, hdrMaxX - filterBtnsW));
                } else {
                    ImGui::SameLine(0.0f, style.ItemSpacing.x);
                }

                auto drawFilterBtn = [&](const char* lbl, bool active, bool targetNearby) {
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.72f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.82f, 1.0f));
                    }
                    if (ImGui::SmallButton(lbl) && !layout.measuring()) {
                        nearbyOnlyFilter_ = targetNearby;
                    }
                    if (active) {
                        ImGui::PopStyleColor(2);
                    }
                };
                drawFilterBtn(allFilterTxt, !nearbyOnlyFilter_, false);
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                drawFilterBtn(nearbyFilterTxt, nearbyOnlyFilter_, true);
            }
            ImGui::Separator();

            if (!layout.measuring()) {
                listedPlayersScratch_.clear();
                if (view.nearbyListEnabled && nearbyOnlyFilter_ && precomputedNearbyOrder != nullptr) {
                    for (const std::size_t idx : *precomputedNearbyOrder) {
                        if (idx >= view.onlinePlayers.size()) {
                            continue;
                        }
                        const auto& p = view.onlinePlayers[idx];
                        if (p.name.empty() || !p.streamedIn) {
                            continue;
                        }
                        if (!filter.empty()) {
                            char rowLabel[96];
                            std::snprintf(rowLabel, sizeof(rowLabel), "%s [%u]", p.name.c_str(), static_cast<unsigned>(p.id));
                            if (!containsIgnoreCase(rowLabel, filter)) {
                                continue;
                            }
                        }
                        listedPlayersScratch_.push_back(&p);
                    }
                } else {
                    for (const auto& p : view.onlinePlayers) {
                        if (p.name.empty()) {
                            continue;
                        }
                        if (view.nearbyListEnabled && nearbyOnlyFilter_ && !p.streamedIn) {
                            continue;
                        }
                        if (!filter.empty()) {
                            char rowLabel[96];
                            std::snprintf(rowLabel, sizeof(rowLabel), "%s [%u]", p.name.c_str(), static_cast<unsigned>(p.id));
                            if (!containsIgnoreCase(rowLabel, filter)) {
                                continue;
                            }
                        }
                        listedPlayersScratch_.push_back(&p);
                    }
                    if (view.nearbyListEnabled && nearbyOnlyFilter_) {
                        std::stable_sort(listedPlayersScratch_.begin(), listedPlayersScratch_.end(),
                                         [](const BlacklistOnlinePlayerView* a, const BlacklistOnlinePlayerView* b) {
                                             if (a->distanceMeters != b->distanceMeters) {
                                                 return a->distanceMeters < b->distanceMeters;
                                             }
                                             return a->id < b->id;
                                         });
                    }
                }

                if (listedPlayersScratch_.empty() && view.nearbyListEnabled && nearbyOnlyFilter_) {
                    ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistNearbyEmpty));
                }

                for (const BlacklistOnlinePlayerView* pPtr : listedPlayersScratch_) {
                    const auto& p = *pPtr;
                    char rowLabel[96];
                    std::snprintf(rowLabel, sizeof(rowLabel), "%s [%u]", p.name.c_str(), static_cast<unsigned>(p.id));

                    const int volPct = cfg.getPlayerVolumePct(p.name, view.serverEndpoint);
                    char volBtnDisplay[16]{};
                    char volBtnBuf[24]{};
                    std::snprintf(volBtnDisplay, sizeof(volBtnDisplay), "%d%%", volPct);
                    std::snprintf(volBtnBuf, sizeof(volBtnBuf), "%d%%##vol", volPct);
                    const float volBtnW = calcBtnWidth(volBtnDisplay);

                    const config::BlacklistEntry* blocking   = cfg.findBlockingEntry(p.name, view.serverEndpoint);
                    const config::BlacklistEntry* originHere = cfg.findOriginEntry(p.name, view.serverEndpoint);

                    float blockBtnsW = pairBtnW;
                    if (blocking) {
                        blockBtnsW = calcBtnWidth(originHere ? unmuteTxt : unmuteHereTxt);
                    }
                    const float btnsW = volBtnW + style.ItemInnerSpacing.x + blockBtnsW;

                    const bool showStatus = view.voiceStatusEnabled &&
                                            p.voiceState != protocol::PlayerVoiceState::Ok;
                    IDirect3DTexture9* statusTex = nullptr;
                    const char* statusBadge = nullptr;
                    const char* statusTooltip = nullptr;
                    ImVec4 statusCol(0.75f, 0.77f, 0.82f, 0.94f);
                    if (showStatus) {
                        if (p.voiceState == protocol::PlayerVoiceState::CannotHear) {
                            statusTex     = view.textures ? view.textures->speakerDeaf : nullptr;
                            statusBadge   = loc.tr(TextKey::VoiceStatusNoHearBadge);
                            statusTooltip = loc.tr(TextKey::VoiceStatusNoHearTooltip);
                            statusCol     = ImVec4(0.88f, 0.42f, 0.37f, 0.95f);
                        } else {
                            statusTex     = view.textures ? view.textures->microMissing : nullptr;
                            statusBadge   = loc.tr(TextKey::VoiceStatusNoMicBadge);
                            statusTooltip = loc.tr(TextKey::VoiceStatusNoMicTooltip);
                            statusCol     = ImVec4(0.75f, 0.77f, 0.82f, 0.94f);
                        }
                    }

                    ImGui::PushID(static_cast<int>(p.id));
                    const float winX         = ImGui::GetWindowPos().x;
                    const float rowStart     = ImGui::GetCursorPosX();
                    const float availRowW    = leftW - style.WindowPadding.x * 2.0f - style.ScrollbarSize;
                    const float contentAvail = rowStart + availRowW;

                    const float statusBlockW = (showStatus && statusBadge)
                        ? (kStatusIconPx + 4.0f + ImGui::CalcTextSize(statusBadge).x + style.ItemSpacing.x)
                        : 0.0f;
                    const float maxNameW = (std::max)(40.0f, availRowW - btnsW - statusBlockW - style.ItemSpacing.x);
                    fitUtf8TextToWidthInto(rowLabel, maxNameW, fittedNameScratch_);

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(fittedNameScratch_.c_str());
                    if (fittedNameScratch_ != rowLabel && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", rowLabel);
                    }
                    float lineEndX = ImGui::GetItemRectMax().x - winX;

                    if (showStatus && statusBadge) {
                        ImGui::SameLine(0.0f, style.ItemSpacing.x);
                        ImGui::BeginGroup();
                        const float lineH    = ImGui::GetFrameHeight();
                        const float yOff     = (std::max)(0.0f, (lineH - kStatusIconPx) * 0.5f);
                        const ImVec2 pCursor = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(kStatusIconPx, lineH));
                        if (statusTex) {
                            ImGui::GetWindowDrawList()->AddImage(
                                reinterpret_cast<ImTextureID>(statusTex),
                                ImVec2(pCursor.x, pCursor.y + yOff),
                                ImVec2(pCursor.x + kStatusIconPx, pCursor.y + yOff + kStatusIconPx));
                        }
                        ImGui::SameLine(0.0f, 4.0f);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(statusCol, "%s", statusBadge);
                        ImGui::EndGroup();
                        if (statusTooltip && ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", statusTooltip);
                        }
                        lineEndX = ImGui::GetItemRectMax().x - winX;
                    }

                    ImGui::SameLine((std::max)(lineEndX + style.ItemSpacing.x, contentAvail - btnsW));

                    const bool nonDefaultVol = (volPct != config::kDefaultPlayerVolume);
                    if (nonDefaultVol) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.42f, 0.14f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.52f, 0.18f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.75f, 0.58f, 0.20f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.00f, 0.92f, 0.65f, 1.00f));
                    }
                    if (ImGui::SmallButton(volBtnBuf)) {
                        volDragActive_ = false;
                        ImGui::OpenPopup("VolPop");
                    }
                    if (nonDefaultVol) {
                        ImGui::PopStyleColor(4);
                    }
                    if (ImGui::BeginPopup("VolPop", ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::TextUnformatted(loc.tr(TextKey::PlayerVolumePopupTitle));
                        ImGui::TextDisabled("%s [%u]", p.name.c_str(), static_cast<unsigned>(p.id));
                        const int beforePct = volPct;
                        int editVol = volPct;
                        SliderArrowResult volRes{};
                        const bool volMoved = sliderIntWithArrows(
                            "##PlayerVol", editVol, 0, config::kMaxPlayerVolume, 1, "%d%%", 200.0f, nullptr, &volRes);
                        if (volRes.sliderActivated) {
                            volDragActive_ = true;
                            volDragInitialPct_ = beforePct;
                        }
                        if (volMoved && volRes.changedByArrow) {
                            volDragActive_ = false;
                            if (cfg.setPlayerVolumePct(p.name, view.serverName, view.serverEndpoint, editVol, false, true)) {
                                dirty = true;
                            }
                            if (editVol != beforePct) {
                                SVPLUS_LOG_INF_CAT(
                                    "config",
                                    "Changed player_volume for '%s' (%s): %d%% -> %d%%",
                                    p.name.c_str(),
                                    view.serverEndpoint.c_str(),
                                    beforePct,
                                    editVol);
                            }
                        } else if (volMoved && volRes.changedBySlider) {
                            (void)cfg.setPlayerVolumePct(
                                p.name, view.serverName, view.serverEndpoint, editVol, false, false);
                        }
                        if (volRes.sliderDeactivatedAfterEdit) {
                            const int startPct = volDragActive_ ? volDragInitialPct_ : beforePct;
                            volDragActive_ = false;
                            if (cfg.setPlayerVolumePct(
                                    p.name, view.serverName, view.serverEndpoint, editVol, false, true) ||
                                editVol != startPct) {
                                dirty = true;
                            }
                            if (editVol != startPct) {
                                SVPLUS_LOG_INF_CAT(
                                    "config",
                                    "Changed player_volume for '%s' (%s): %d%% -> %d%%",
                                    p.name.c_str(),
                                    view.serverEndpoint.c_str(),
                                    startPct,
                                    editVol);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button((std::string(loc.tr(TextKey::ResetButton)) + "##ResetPlayerVol").c_str())) {
                            const int beforeReset = cfg.getPlayerVolumePct(p.name, view.serverEndpoint);
                            volDragActive_ = false;
                            if (cfg.setPlayerVolumePct(p.name, view.serverName, view.serverEndpoint,
                                                       config::kDefaultPlayerVolume, false, true)) {
                                dirty = true;
                            }
                            if (beforeReset != config::kDefaultPlayerVolume) {
                                SVPLUS_LOG_INF_CAT(
                                    "config",
                                    "Changed player_volume for '%s' (%s): %d%% -> %d%%",
                                    p.name.c_str(),
                                    view.serverEndpoint.c_str(),
                                    beforeReset,
                                    config::kDefaultPlayerVolume);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button((std::string(loc.tr(TextKey::PopupOk)) + "##ClosePlayerVol").c_str())) {
                            volDragActive_ = false;
                            ImGui::CloseCurrentPopup();
                        }
                        if (!ImGui::GetIO().WantTextInput &&
                            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
                            volDragActive_ = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                    if (!blocking) {
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
                        if (ImGui::SmallButton(unmuteTxt)) {
                            if (cfg.removeBlacklistEntry(p.name, view.serverEndpoint)) {
                                dirty = true;
                            }
                        }
                    } else {
                        if (ImGui::SmallButton(unmuteHereTxt)) {
                            if (cfg.setBlacklistEntryScope(blocking->playerName, blocking->serverEndpoint,
                                                           config::BlacklistScope::Server)) {
                                dirty = true;
                            }
                        }
                        const std::string originLabel =
                            !blocking->serverName.empty() ? blocking->serverName : blocking->serverEndpoint;
                        char originNote[256]{};
                        std::snprintf(originNote, sizeof(originNote), loc.tr(TextKey::BlacklistMutedAllFromFmt),
                                      originLabel.c_str());
                        ImGui::Indent(12.0f);
                        fitUtf8TextToWidthInto(originNote, (std::max)(60.0f, availRowW - 14.0f), fittedCellScratch_);
                        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s", fittedCellScratch_.c_str());
                        if (fittedCellScratch_ != originNote && ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", originNote);
                        }
                        ImGui::Unindent(12.0f);
                    }
                    ImGui::PopID();
                }
            }
        }
        layout.endListChild();

        if (!stackPart1) {
            ImGui::SameLine();
        }

        // Right child: Blocked entries active on current server
        activeEntriesScratch_.clear();
        std::size_t clearableCount = 0;
        if (!layout.measuring()) {
            for (const auto& e : cfg.blacklistEntries) {
                const bool isLocal = equalsIgnoreCase(e.serverEndpoint, view.serverEndpoint);
                if (isLocal || e.scope == config::BlacklistScope::All) {
                    activeEntriesScratch_.push_back(&e);
                    if (isLocal) {
                        ++clearableCount;
                    }
                }
            }
        }

        if (layout.beginListChild("##ActiveMuted", rightW, rightH, true)) {
            const float hdrMaxX = ImGui::GetCursorPosX() + rightW - style.WindowPadding.x * 2.0f -
                                  (layout.measuring() ? style.ScrollbarSize : 0.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s (%zu)", loc.tr(TextKey::BlacklistMutedList), activeEntriesScratch_.size());
            if (layout.visible(clearableCount > 0)) {
                const float endX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
                if (endX + style.ItemSpacing.x + clrW <= hdrMaxX) {
                    ImGui::SameLine((std::max)(endX + style.ItemSpacing.x, hdrMaxX - clrW));
                } else {
                    ImGui::SameLine(0.0f, style.ItemSpacing.x);
                }
                if (ImGui::SmallButton(clrTxt) && !layout.measuring()) {
                    if (cfg.clearOriginServerBlacklist(view.serverEndpoint) > 0) {
                        dirty = true;
                    }
                }
            }
            ImGui::Separator();

            if (!layout.measuring()) {
                if (activeEntriesScratch_.empty()) {
                    ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistEmpty));
                } else {
                    for (std::size_t idx = 0; idx < activeEntriesScratch_.size(); ++idx) {
                        const auto* e = activeEntriesScratch_[idx];
                        if (!containsIgnoreCase(e->playerName, filter) &&
                            !containsIgnoreCase(e->serverName, filter) &&
                            !containsIgnoreCase(e->serverEndpoint, filter)) {
                            continue;
                        }
                        const bool isAll = (e->scope == config::BlacklistScope::All);
                        const char* scopeTag = isAll ? scopeAllTxt : scopeSrvTxt;
                        char scopeBuf[64]{};
                        std::snprintf(scopeBuf, sizeof(scopeBuf), "[%s]", scopeTag);
                        const float scopeW = ImGui::CalcTextSize(scopeBuf).x;

                        ImGui::PushID(static_cast<int>(2000 + idx));
                        const float rowStart   = ImGui::GetCursorPosX();
                        const float availRowW  = rightW - style.WindowPadding.x * 2.0f - style.ScrollbarSize;
                        const float nameMaxW   = (std::max)(40.0f, availRowW - scopeW - rmW - style.ItemSpacing.x * 2.0f);
                        fitUtf8TextToWidthInto(e->playerName, nameMaxW, fittedNameScratch_);

                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(fittedNameScratch_.c_str());
                        if (fittedNameScratch_ != e->playerName && ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", e->playerName.c_str());
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", scopeBuf);
                        ImGui::SameLine(rowStart + (std::max)(0.0f, availRowW - rmW));
                        if (ImGui::SmallButton(rmTxt)) {
                            const std::string pCopy  = e->playerName;
                            const std::string epCopy = e->serverEndpoint;
                            if (cfg.removeBlacklistEntry(pCopy, epCopy)) {
                                dirty = true;
                            }
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        layout.endListChild();
    }

    // =========================================================================
    // Part 2: Full blacklist across all servers (always visible)
    // =========================================================================
    ImGui::Spacing();
    char allTitleBuf[128]{};
    std::snprintf(allTitleBuf, sizeof(allTitleBuf), "%s (%zu)",
                  loc.tr(TextKey::BlacklistAllHeader), cfg.blacklistEntries.size());
    const float allHeaderMinW = ImGui::CalcTextSize(loc.tr(TextKey::BlacklistAllHeader)).x + 48.0f;
    const float allSearchW    = layout.flexWidth(140.0f, 300.0f, allHeaderMinW + style.ItemSpacing.x);
    const float part2Width    = (layout.measuring() && layout.passWidth() <= 0.0f)
        ? (layout.mode() == MeasureMode::MeasureMin ? part2MinW : part2PrefW)
        : (std::max)(part2MinW, availW);

    drawHeaderWithRightControl(allTitleBuf, allSearchW, ImVec4(0.86f, 0.90f, 0.96f, 1.0f), part2Width);
    placeRightOnSameLine(part2Width, allSearchW);
    ImGui::SetNextItemWidth(allSearchW);
    ImGui::InputTextWithHint("##BlacklistAllSearch", loc.tr(TextKey::BlacklistAllSearchPlaceholder),
                             allSearchBuf_, sizeof(allSearchBuf_));

    if (!connectedView) {
        const std::array<float, 1> mins{minPart2H};
        const std::array<float, 1> weights{1.0f};
        std::array<float, 1> heights{};
        const float budget = layout.measuring()
            ? ((layout.mode() == MeasureMode::MeasurePreferred) ? prefPart2H : 0.0f)
            : (std::max)(0.0f, layout.availHeight() - bottomReserve);
        layout.distributeHeights(mins, weights, budget, heights);
        part2H = heights[0];
    }
    const std::string_view allFilter(allSearchBuf_);

    if (!layout.measuring() && cfg.blacklistEntries.empty()) {
        if (layout.beginListChild("##AllBlacklistEmpty", part2Width, part2H, true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextDisabled("%s", loc.tr(TextKey::BlacklistEmpty));
        }
        layout.endListChild();
        return;
    }

    if (!layout.measuring()) {
        sortedBlacklistIndicesScratch_.clear();
        for (std::size_t i = 0; i < cfg.blacklistEntries.size(); ++i) {
            const auto& e = cfg.blacklistEntries[i];
            if (!containsIgnoreCase(e.playerName, allFilter) &&
                !containsIgnoreCase(e.serverName, allFilter) &&
                !containsIgnoreCase(e.serverEndpoint, allFilter)) {
                continue;
            }
            sortedBlacklistIndicesScratch_.push_back(i);
        }
        std::sort(sortedBlacklistIndicesScratch_.begin(), sortedBlacklistIndicesScratch_.end(), [&](std::size_t ia, std::size_t ib) {
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
    }

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingFixedFit;

    if (layout.beginTableSafe("##AllBlacklistTable", 5, kTableFlags, part2Width, part2H)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColPlayer),   ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColServer),   ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColEndpoint), ImGuiTableColumnFlags_WidthFixed, epColW);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColScope),    ImGuiTableColumnFlags_WidthFixed, scopeColW);
        ImGui::TableSetupColumn(loc.tr(TextKey::BlacklistColActions),  ImGuiTableColumnFlags_WidthFixed, actionsColW);
        ImGui::TableHeadersRow();

        if (!layout.measuring()) {
            enum class RowAction { None, SetOrigin, SetAll, Remove };
            RowAction pendingAction = RowAction::None;
            std::string actionPlayer;
            std::string actionEp;

            const float stretchAvail = (std::max)(160.0f, part2Width - epColW - scopeColW - actionsColW - style.ScrollbarSize - 24.0f);
            const float playerColEst = (std::max)(60.0f, stretchAvail * 0.40f - style.CellPadding.x * 2.0f);
            const float serverColEst = (std::max)(60.0f, stretchAvail * 0.60f - style.CellPadding.x * 2.0f);

            for (std::size_t rowIdx : sortedBlacklistIndicesScratch_) {
                const auto& e = cfg.blacklistEntries[rowIdx];
                const bool isAll = (e.scope == config::BlacklistScope::All);
                const char* scopeLabel = isAll ? scopeAllTxt : scopeSrvTxt;

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(5000 + rowIdx));

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                fitUtf8TextToWidthInto(e.playerName, playerColEst, fittedCellScratch_);
                ImGui::TextUnformatted(fittedCellScratch_.c_str());
                if (fittedCellScratch_ != e.playerName && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", e.playerName.c_str());
                }

                ImGui::TableSetColumnIndex(1);
                const std::string& rawSrv = e.serverName;
                const char* srvFull = rawSrv.empty() ? "-" : rawSrv.c_str();
                fitUtf8TextToWidthInto(srvFull, serverColEst, fittedCellScratch_);
                ImGui::TextUnformatted(fittedCellScratch_.c_str());
                if (!rawSrv.empty() && fittedCellScratch_ != rawSrv && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", rawSrv.c_str());
                }

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
                if (ImGui::SmallButton(muteAllTxt)) {
                    pendingAction = RowAction::SetAll;
                    actionPlayer  = e.playerName;
                    actionEp      = e.serverEndpoint;
                }
                ImGui::EndDisabled();

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (ImGui::SmallButton(rmTxt)) {
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
        }

        layout.endTableSafe();
    }
}

void ConfigMenu::renderBlacklistTab(config::ClientConfig& cfg,
                                    Localization& loc,
                                    const UiTextures& textures,
                                    MenuLayout& layout,
                                    bool& dirty) {
    if (layout.measuring()) {
        BlacklistTabView measureView{};
        measureView.connected          = true;
        measureView.voiceStatusEnabled = true;
        measureView.nearbyListEnabled  = true;
        measureView.textures           = &textures;
        renderBlacklistTabImpl(cfg, loc, measureView, nullptr, layout, dirty);
        return;
    }

    const auto& samp  = samp::SampBridge::instance();
    const auto& voice = net::VoiceClient::instance();

    const bool connected          = samp.isConnected();
    const bool voiceStatusEnabled = voice.isServerPlus() && voice.isVoiceStatusEnabled();
    const bool nearbyListEnabled  = voice.isServerPlus() && voice.isNearbyListEnabled();
    const std::size_t connectedCount = connected ? samp.getConnectedRemotePlayerCount() : 0u;
    const auto now = std::chrono::steady_clock::now();

    const bool needRebuild =
        !hasBlacklistCache_ ||
        connected != cachedBlacklistView_.connected ||
        voiceStatusEnabled != cachedBlacklistView_.voiceStatusEnabled ||
        nearbyListEnabled != cachedBlacklistView_.nearbyListEnabled ||
        connectedCount != lastConnectedRemoteCount_ ||
        (now - lastBlacklistRebuild_) >= std::chrono::milliseconds(250);

    if (needRebuild) {
        hasBlacklistCache_        = true;
        lastBlacklistRebuild_     = now;
        lastConnectedRemoteCount_ = connectedCount;

        cachedBlacklistView_.connected          = connected;
        cachedBlacklistView_.voiceStatusEnabled = voiceStatusEnabled;
        cachedBlacklistView_.nearbyListEnabled  = nearbyListEnabled;

        if (!connected) {
            cachedBlacklistView_.serverEndpoint.clear();
            cachedBlacklistView_.serverName.clear();
            cachedBlacklistView_.onlinePlayers.clear();
            cachedNearbyOrder_.clear();
        } else {
            cachedBlacklistView_.serverEndpoint = samp.getServerEndpointKey();
            cachedBlacklistView_.serverName     = samp.getServerHostname();

            samp.collectConnectedRemotePlayers(remotePlayerScratch_, nearbyListEnabled);
            const std::size_t n = remotePlayerScratch_.size();
            if (cachedBlacklistView_.onlinePlayers.size() < n) {
                cachedBlacklistView_.onlinePlayers.resize(n);
            }
            for (std::size_t i = 0; i < n; ++i) {
                const auto& src = remotePlayerScratch_[i];
                auto& dst = cachedBlacklistView_.onlinePlayers[i];
                dst.id = src.id;
                util::ensureUtf8NameInto(std::string_view(src.rawName.data(), src.rawNameLen), dst.name);
                dst.voiceState     = voice.playerVoiceState(src.id);
                dst.streamedIn     = src.streamedIn;
                dst.distanceMeters = src.distanceMeters;
            }
            if (cachedBlacklistView_.onlinePlayers.size() > n) {
                cachedBlacklistView_.onlinePlayers.resize(n);
            }

            cachedNearbyOrder_.clear();
            if (nearbyListEnabled) {
                for (std::size_t i = 0; i < n; ++i) {
                    if (cachedBlacklistView_.onlinePlayers[i].streamedIn) {
                        cachedNearbyOrder_.push_back(i);
                    }
                }
                std::stable_sort(cachedNearbyOrder_.begin(), cachedNearbyOrder_.end(),
                                 [&](std::size_t ia, std::size_t ib) {
                                     const auto& a = cachedBlacklistView_.onlinePlayers[ia];
                                     const auto& b = cachedBlacklistView_.onlinePlayers[ib];
                                     if (a.distanceMeters != b.distanceMeters) {
                                         return a.distanceMeters < b.distanceMeters;
                                     }
                                     return a.id < b.id;
                                 });
            }
        }
    }

    cachedBlacklistView_.textures = &textures;
    renderBlacklistTabImpl(cfg, loc, cachedBlacklistView_, &cachedNearbyOrder_, layout, dirty);
}

} // namespace svplus::client::ui
