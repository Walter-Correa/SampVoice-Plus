#pragma once

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

namespace svplus::client::ui {

inline constexpr float kListMinRowsPart1         = 3.0f;
inline constexpr float kListPrefRowsPart1        = 5.0f;
inline constexpr float kListMinRowsAllBlacklist  = 3.0f;
inline constexpr float kListPrefRowsAllBlacklist = 6.0f;

enum class MeasureMode : std::uint8_t {
    Normal = 0,
    MeasureMin,
    MeasurePreferred,
};

class MenuLayout;

struct MenuTabSpec {
    const char*                      id{nullptr};
    const char*                      label{nullptr};
    std::function<void(MenuLayout&)> draw{};
};

struct MeasuredMenuDimensions {
    ImVec2                     minSize{0.0f, 0.0f};        // {Wmin, HminNarrow}
    float                      minHeightAtWide{0.0f};      // HminWide (measured at Wdef with MeasureMin)
    ImVec2                     preferredSize{0.0f, 0.0f};  // {Wdef, Hdef}
    int                        minWidthTabIdx{-1};
    int                        minHeightNarrowTabIdx{-1};
    int                        minHeightWideTabIdx{-1};
    int                        prefWidthTabIdx{-1};
    int                        prefHeightTabIdx{-1};
    float                      lastMeasureUs{0.0f};
    std::uint64_t              chromeCondSig{0};
    std::vector<std::uint64_t> tabCondSigs{};
};

/// Adaptive layout helper shared by ConfigMenu chrome, tabs, and off-screen measurement.
class MenuLayout {
public:
    explicit MenuLayout(MeasureMode mode = MeasureMode::Normal, float passWidth = 0.0f) noexcept
        : mode_(mode), passWidth_(passWidth) {}

    [[nodiscard]] MeasureMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool measuring() const noexcept { return mode_ != MeasureMode::Normal; }
    [[nodiscard]] float passWidth() const noexcept { return passWidth_; }

    /// Current available width inside the window/panel without exposing raw GetContentRegionAvail outside MenuLayout.
    [[nodiscard]] float availWidth() const noexcept;
    [[nodiscard]] float availHeight() const noexcept;

    /// Width that adapts between [minW, prefW] based on available width minus rightReserve.
    [[nodiscard]] float flexWidth(float minW, float prefW, float rightReserve = 0.0f) const noexcept;

    /// Width for stretchable panels/tables that expand to fill availWidth() - rightReserve when space permits.
    [[nodiscard]] float fillWidth(float minW, float prefW, float rightReserve = 0.0f) const noexcept;

    /// List/panel height expressed in font lines (uses line height, not hardcoded pixels).
    [[nodiscard]] float flexRows(float minRows,
                                 float prefRows,
                                 float headerExtraPx = 0.0f,
                                 float reserveBelowPx = 0.0f) const noexcept;

    /// Distributes `availH` across stacked regions so every region receives at least `mins[i]`
    /// and any surplus above `sum(mins)` is split proportionally by `weights[i]`.
    /// In `MeasureMin` (or when `availH <= sum(mins)`), every region receives its exact minimum `mins[i]`.
    void distributeHeights(std::span<const float> mins,
                           std::span<const float> weights,
                           float availH,
                           std::span<float> outHeights) const noexcept;

    /// Reserves vertical separator spacing matching ImGui::Separator() with 0 measured width.
    /// When `width > 0` and not measuring, draws the horizontal separator rect within `[cursorX, cursorX + width]`.
    float columnSeparator(float width = 0.0f) const noexcept;

    /// Draws a 1 px horizontal separator rect matching ImGui::Separator() strictly within `[screenX, screenX + width]`.
    void drawColumnSeparatorLine(float screenX, float screenY, float width) const noexcept;

    /// Evaluates real visibility condition in both normal and measurement passes and records its signature.
    [[nodiscard]] bool visible(bool condition) const noexcept {
        if (condition) {
            condSig_ ^= 0x9e3779b97f4a7c15ULL +
                        (static_cast<std::uint64_t>(condCount_) + 1ULL) * 0xbf58476d1ce4e5b9ULL;
        }
        condSig_ = (condSig_ << 7) | (condSig_ >> 57);
        condSig_ ^= static_cast<std::uint64_t>(condCount_) + 1ULL;
        ++condCount_;
        return condition;
    }

    void resetCondTrack() const noexcept {
        condSig_   = 0x517cc1b727220a95ULL;
        condCount_ = 0u;
    }
    [[nodiscard]] std::uint64_t condSignature() const noexcept { return condSig_; }

    /// Picks the widest text among variants when measuring, returning actual when rendering normally.
    [[nodiscard]] const char* longestText(const char* actual,
                                          std::initializer_list<const char*> variants) const noexcept;

    /// Wraps text starting at current cursor position or drops to a new line if remaining space < minWrapWidth.
    void sameLineWrappedText(const char* text, float minWrapWidth = 140.0f, bool disabled = true) const noexcept;
    void wrappedText(const char* text, float minWrapWidth = 140.0f, float prefWrapWidth = 360.0f) const noexcept;

    /// Explicit-size child container wrappers (never passing 0 or -FLT_MIN width).
    bool beginListChild(const char* strId,
                        float width,
                        float height,
                        bool border = true,
                        ImGuiWindowFlags extraFlags = 0) const noexcept;
    void endListChild() const noexcept;

    /// Explicit-size table wrapper (never passing 0 or -FLT_MIN width).
    bool beginTableSafe(const char* strId,
                        int columns,
                        ImGuiTableFlags flags,
                        float width,
                        float height = 0.0f) const noexcept;
    void endTableSafe() const noexcept;

private:
    MeasureMode            mode_{MeasureMode::Normal};
    float                  passWidth_{0.0f};
    mutable std::uint64_t  condSig_{0};
    mutable std::uint32_t  condCount_{0};
};

/// Measures exact window min (Wmin x Hmin) and preferred (Wdef x Hdef) dimensions across all tabs
/// using an invisible off-screen ImGui window and public BeginGroup/EndGroup APIs.
[[nodiscard]] MeasuredMenuDimensions measureMenuWindowDimensions(
    std::span<const MenuTabSpec>             tabs,
    const std::function<void(MenuLayout&)>&  drawHeader,
    const std::function<void(MenuLayout&)>&  drawFooter);

} // namespace svplus::client::ui
