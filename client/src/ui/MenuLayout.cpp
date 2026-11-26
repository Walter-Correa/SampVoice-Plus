#include "ui/MenuLayout.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace svplus::client::ui {

float MenuLayout::availWidth() const noexcept {
    return (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
}

float MenuLayout::availHeight() const noexcept {
    return (std::max)(0.0f, ImGui::GetContentRegionAvail().y);
}

float MenuLayout::flexWidth(float minW, float prefW, float rightReserve) const noexcept {
    const float lo = (std::max)(1.0f, minW);
    const float hi = (std::max)(lo, prefW);
    if (mode_ == MeasureMode::MeasureMin && passWidth_ <= 0.0f) {
        return lo;
    }
    if (mode_ == MeasureMode::MeasurePreferred && passWidth_ <= 0.0f) {
        return hi;
    }
    return std::clamp(availWidth() - rightReserve, lo, hi);
}

float MenuLayout::fillWidth(float minW, float prefW, float rightReserve) const noexcept {
    const float lo = (std::max)(1.0f, minW);
    const float hi = (std::max)(lo, prefW);
    if (mode_ == MeasureMode::MeasureMin && passWidth_ <= 0.0f) {
        return lo;
    }
    if (mode_ == MeasureMode::MeasurePreferred && passWidth_ <= 0.0f) {
        return hi;
    }
    return (std::max)(lo, availWidth() - rightReserve);
}

float MenuLayout::flexRows(float minRows,
                           float prefRows,
                           float headerExtraPx,
                           float reserveBelowPx) const noexcept {
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const float minH  = (std::max)(1.0f, minRows * lineH + headerExtraPx);
    const float prefH = (std::max)(minH, prefRows * lineH + headerExtraPx);
    if (mode_ == MeasureMode::MeasureMin) {
        return minH;
    }
    if (mode_ == MeasureMode::MeasurePreferred) {
        return prefH;
    }
    return (std::max)(minH, availHeight() - reserveBelowPx);
}

void MenuLayout::distributeHeights(std::span<const float> mins,
                                   std::span<const float> weights,
                                   float availH,
                                   std::span<float> outHeights) const noexcept {
    const std::size_t n = mins.size();
    if (n == 0 || outHeights.size() < n) {
        return;
    }
    double sumMin = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const float lo = (std::max)(1.0f, mins[i]);
        outHeights[i] = lo;
        sumMin += static_cast<double>(lo);
    }
    if (mode_ == MeasureMode::MeasureMin) {
        return;
    }
    const double targetTotal = (std::max)(sumMin, static_cast<double>(availH));
    const double surplus = targetTotal - sumMin;
    if (surplus <= 0.0) {
        return;
    }
    double sumW = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i < weights.size() && weights[i] > 0.0f) {
            sumW += static_cast<double>(weights[i]);
        }
    }
    double allocated = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i + 1u == n) {
            const double rem = targetTotal - allocated;
            const double floored = std::floor(rem + 1e-4);
            outHeights[i] = static_cast<float>((std::max)(static_cast<double>(outHeights[i]), floored));
        } else {
            const double share = (sumW > 0.0)
                ? ((i < weights.size() && weights[i] > 0.0f) ? (static_cast<double>(weights[i]) / sumW) : 0.0)
                : (1.0 / static_cast<double>(n));
            const double raw = static_cast<double>(outHeights[i]) + surplus * share;
            const double floored = (std::max)(static_cast<double>(outHeights[i]), std::floor(raw));
            outHeights[i] = static_cast<float>(floored);
            allocated += floored;
        }
    }
}

float MenuLayout::columnSeparator(float width) const noexcept {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    // Matches ImGui::SeparatorEx(Horizontal, 1.0f): zero horizontal contribution and identical ItemSpacing.y rhythm.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    if (!measuring() && width > 0.0f) {
        drawColumnSeparatorLine(pos.x, pos.y, width);
    }
    return pos.y;
}

void MenuLayout::drawColumnSeparatorLine(float screenX, float screenY, float width) const noexcept {
    if (measuring() || width <= 0.0f) {
        return;
    }
    const ImU32 sepCol = ImGui::GetColorU32(ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(screenX, screenY),
        ImVec2(screenX + width, screenY + 1.0f),
        sepCol);
}

const char* MenuLayout::longestText(const char* actual,
                                    std::initializer_list<const char*> variants) const noexcept {
    if (!measuring()) {
        return actual ? actual : "";
    }
    const char* best = actual ? actual : "";
    float bestW = best[0] ? ImGui::CalcTextSize(best).x : 0.0f;
    for (const char* v : variants) {
        if (!v || v[0] == '\0') {
            continue;
        }
        const float w = ImGui::CalcTextSize(v).x;
        if (w > bestW) {
            bestW = w;
            best  = v;
        }
    }
    return best;
}

void MenuLayout::sameLineWrappedText(const char* text, float minWrapWidth, bool disabled) const noexcept {
    if (!text) {
        return;
    }
    const float effectiveMin = (std::max)(40.0f, minWrapWidth);
    float wrapW = effectiveMin;

    if (measuring() && passWidth_ <= 0.0f) {
        if (mode_ == MeasureMode::MeasureMin) {
            // In minimum width pass, allow dropping below so side notes do not artificially inflate Wmin.
            ImGui::NewLine();
            wrapW = effectiveMin;
        } else {
            // In preferred width pass, let short/moderate side tips sit cleanly on one line up to 340 px.
            ImGui::SameLine();
            const float textW = ImGui::CalcTextSize(text).x + 2.0f;
            wrapW = (std::clamp)(textW, effectiveMin, 340.0f);
        }
    } else {
        const float lineStartX = ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x;
        const float maxLocalX  = lineStartX + availWidth();
        ImGui::SameLine();
        const float remOnLine = maxLocalX - ImGui::GetCursorScreenPos().x;
        if (remOnLine >= effectiveMin) {
            wrapW = remOnLine;
        } else {
            ImGui::NewLine();
            wrapW = availWidth();
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(1.0f, wrapW));
    if (disabled) {
        ImGui::TextDisabled("%s", text);
    } else {
        ImGui::TextUnformatted(text);
    }
    ImGui::PopTextWrapPos();
}

void MenuLayout::wrappedText(const char* text, float minWrapWidth, float prefWrapWidth) const noexcept {
    if (!text) {
        return;
    }
    float wrapW = availWidth();
    if (measuring() && passWidth_ <= 0.0f) {
        wrapW = (mode_ == MeasureMode::MeasureMin)
            ? (std::max)(40.0f, minWrapWidth)
            : (std::max)(minWrapWidth, (std::min)(prefWrapWidth, ImGui::CalcTextSize(text).x + 2.0f));
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(1.0f, wrapW));
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

bool MenuLayout::beginListChild(const char* strId,
                                float width,
                                float height,
                                bool border,
                                ImGuiWindowFlags extraFlags) const noexcept {
    return ImGui::BeginChild(
        strId,
        ImVec2((std::max)(1.0f, width), (std::max)(1.0f, height)),
        border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None,
        extraFlags);
}

void MenuLayout::endListChild() const noexcept {
    ImGui::EndChild();
}

bool MenuLayout::beginTableSafe(const char* strId,
                                int columns,
                                ImGuiTableFlags flags,
                                float width,
                                float height) const noexcept {
    return ImGui::BeginTable(strId, columns, flags, ImVec2((std::max)(1.0f, width), height));
}

void MenuLayout::endTableSafe() const noexcept {
    ImGui::EndTable();
}

MeasuredMenuDimensions measureMenuWindowDimensions(
    std::span<const MenuTabSpec>            tabs,
    const std::function<void(MenuLayout&)>& drawHeader,
    const std::function<void(MenuLayout&)>& drawFooter) {
    MeasuredMenuDimensions out{};
    if (tabs.empty()) {
        return out;
    }

    const auto tStart = std::chrono::steady_clock::now();
    const auto& style = ImGui::GetStyle();
    const float padW      = style.WindowPadding.x * 2.0f;
    const float padH      = style.WindowPadding.y * 2.0f;
    const float titleBarH = ImGui::GetFrameHeight();

    // Natural unclipped TabBar width so Wmin never compresses tab buttons.
    float tabBarNaturalW = 0.0f;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const char* lbl = tabs[i].label ? tabs[i].label : "";
        tabBarNaturalW += ImGui::CalcTextSize(lbl).x + style.FramePadding.x * 2.0f;
        if (i + 1u < tabs.size()) {
            tabBarNaturalW += style.ItemInnerSpacing.x;
        }
    }
    tabBarNaturalW += 4.0f;

    constexpr ImGuiWindowFlags kMeasureFlags =
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar;

    auto combineSig = [](std::uint64_t a, std::uint64_t b) noexcept {
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    };

    out.tabCondSigs.assign(tabs.size(), 0ULL);

    auto measurePass = [&](int passId, MeasureMode mode, float fixedBodyW, bool measureTabBarHeight, bool recordSigs) {
        struct PassResult {
            float maxW{0.0f};
            float maxH{0.0f};
            int   wTab{0};
            int   hTab{0};
        } res{};

        const float winW = (fixedBodyW > 0.0f) ? (fixedBodyW + padW) : 4096.0f;
        const float winH = 4096.0f;

        for (std::size_t tabIdx = 0; tabIdx < tabs.size(); ++tabIdx) {
            char winName[48]{};
            std::snprintf(winName, sizeof(winName), "##SvPlusMeasure_%d_%zu", passId, tabIdx);
            ImGui::SetNextWindowPos(ImVec2(-12000.0f, -12000.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
            if (ImGui::Begin(winName, nullptr, kMeasureFlags)) {
                MenuLayout layout(mode, fixedBodyW);
                std::uint64_t hdrSig = 0ULL;
                std::uint64_t tabSig = 0ULL;
                std::uint64_t ftrSig = 0ULL;

                ImGui::BeginGroup();
                layout.resetCondTrack();
                if (drawHeader) {
                    drawHeader(layout);
                }
                hdrSig = layout.condSignature();

                if (measureTabBarHeight) {
                    if (ImGui::BeginTabBar("##MeasureTabBar")) {
                        // Submit only the target tab so ImGui activates it immediately on frame 1 (Tabs.Size == 1).
                        char tabBuf[128]{};
                        const char* idPart = tabs[tabIdx].id ? tabs[tabIdx].id : "Tab";
                        std::snprintf(tabBuf, sizeof(tabBuf), "%s###%s",
                                      tabs[tabIdx].label ? tabs[tabIdx].label : "", idPart);
                        if (ImGui::BeginTabItem(tabBuf)) {
                            layout.resetCondTrack();
                            if (tabs[tabIdx].draw) {
                                tabs[tabIdx].draw(layout);
                            }
                            tabSig = layout.condSignature();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                } else {
                    ImGui::Dummy(ImVec2(tabBarNaturalW, ImGui::GetFrameHeight()));
                    ImGui::Separator();
                    layout.resetCondTrack();
                    if (tabs[tabIdx].draw) {
                        tabs[tabIdx].draw(layout);
                    }
                    tabSig = layout.condSignature();
                }

                layout.resetCondTrack();
                if (drawFooter) {
                    drawFooter(layout);
                }
                ftrSig = layout.condSignature();
                ImGui::EndGroup();

                if (recordSigs) {
                    if (tabIdx == 0) {
                        out.chromeCondSig = combineSig(hdrSig, ftrSig);
                    }
                    out.tabCondSigs[tabIdx] = tabSig;
                }

                const ImVec2 sz = ImGui::GetItemRectSize();
                if (sz.x > res.maxW) {
                    res.maxW = sz.x;
                    res.wTab = static_cast<int>(tabIdx);
                }
                if (sz.y > res.maxH) {
                    res.maxH = sz.y;
                    res.hTab = static_cast<int>(tabIdx);
                }
            }
            ImGui::End();
        }
        return res;
    };

    // Pass 1: Minimum width across all tabs + tab bar + chrome
    const auto p1 = measurePass(1, MeasureMode::MeasureMin, 0.0f, false, false);
    const float wMinBody = std::ceil((std::max)(p1.maxW, tabBarNaturalW));

    // Pass 2: Minimum height at narrow width Wmin
    const auto p2 = measurePass(2, MeasureMode::MeasureMin, wMinBody, true, true);
    const float hMinNarrowBody = std::ceil(p2.maxH);

    // Pass 3: Preferred width across all tabs (>= Wmin)
    const auto p3 = measurePass(3, MeasureMode::MeasurePreferred, 0.0f, false, false);
    const float wDefBody = std::ceil((std::max)(wMinBody, p3.maxW));

    // Pass 4: Minimum height at default/wide width Wdef (e.g., side-by-side layout when it fits)
    const auto p4 = measurePass(4, MeasureMode::MeasureMin, wDefBody, true, false);
    const float hMinWideBody = std::ceil(p4.maxH);

    // Pass 5: Preferred height at default/wide width Wdef (>= HminWide, independent of HminNarrow)
    const auto p5 = measurePass(5, MeasureMode::MeasurePreferred, wDefBody, true, false);
    const float hDefBody = std::ceil((std::max)(hMinWideBody, p5.maxH));

    out.minSize               = ImVec2(wMinBody + padW, hMinNarrowBody + padH + titleBarH);
    out.minHeightAtWide       = hMinWideBody + padH + titleBarH;
    out.preferredSize         = ImVec2((std::max)(out.minSize.x, wDefBody + padW),
                                       (std::max)(out.minHeightAtWide, hDefBody + padH + titleBarH));
    out.minWidthTabIdx        = p1.wTab;
    out.minHeightNarrowTabIdx = p2.hTab;
    out.minHeightWideTabIdx   = p4.hTab;
    out.prefWidthTabIdx       = p3.wTab;
    out.prefHeightTabIdx      = p5.hTab;

    const auto tEnd = std::chrono::steady_clock::now();
    out.lastMeasureUs = std::chrono::duration<float, std::micro>(tEnd - tStart).count();
    return out;
}

} // namespace svplus::client::ui
