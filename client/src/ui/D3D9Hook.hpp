#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>
#include <windows.h>

#include "svplus/common/Protocol.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>

struct ImFont;

namespace svplus::client::ui {

struct UiTextures {
    IDirect3DTexture9* microActive{nullptr};
    IDirect3DTexture9* microMuted{nullptr};
    IDirect3DTexture9* microPassive{nullptr};
    IDirect3DTexture9* speaker{nullptr};
    IDirect3DTexture9* speakerBlocked{nullptr};
    std::array<IDirect3DTexture9*, protocol::kStreamIconCount> streamIcons{};
    std::array<IDirect3DTexture9*, protocol::kStreamIconCount> streamIconsBlocked{};

    [[nodiscard]] IDirect3DTexture9* streamIconTexture(protocol::StreamIconId iconId) const noexcept {
        const std::size_t idx = static_cast<std::size_t>(iconId);
        if (idx < streamIcons.size() && streamIcons[idx]) {
            return streamIcons[idx];
        }
        return speaker;
    }

    [[nodiscard]] IDirect3DTexture9* streamIconTextureBlocked(protocol::StreamIconId iconId) const noexcept {
        const std::size_t idx = static_cast<std::size_t>(iconId);
        if (idx < streamIconsBlocked.size() && streamIconsBlocked[idx]) {
            return streamIconsBlocked[idx];
        }
        return speakerBlocked ? speakerBlocked : streamIconTexture(iconId);
    }

    [[nodiscard]] IDirect3DTexture9* streamIconTexture(protocol::StreamIconId iconId, bool blocked) const noexcept {
        return blocked ? streamIconTextureBlocked(iconId) : streamIconTexture(iconId);
    }
};

class D3D9Hook {
public:
    using FrameCallback = std::function<void(IDirect3DDevice9*, HWND, const UiTextures&)>;
    using WndProcHook   = std::function<bool(HWND, UINT, WPARAM, LPARAM)>;

    static D3D9Hook& instance() noexcept;

    [[nodiscard]] bool initialize(FrameCallback onFrame, WndProcHook onWndProc, bool suppressRetryLog = false);
    void shutdown() noexcept;
    void unsubclassWndProc() noexcept;
    void setMenuOpenState(bool open) noexcept;

    [[nodiscard]] bool isInitialized() const noexcept { return hooksInstalled_.load(std::memory_order_acquire); }
    [[nodiscard]] HWND gameWindow() const noexcept { return gameHwnd_; }
    [[nodiscard]] HWND gameWindowAtomic() const noexcept { return gameHwndAtomic_.load(std::memory_order_acquire); }
    [[nodiscard]] IDirect3DDevice9* device() const noexcept { return boundDevice_; }
    [[nodiscard]] ImFont* titleFont() const noexcept { return titleFont_; }
    [[nodiscard]] ImFont* badgeFont() const noexcept { return badgeFont_; }

private:
    D3D9Hook() = default;
    ~D3D9Hook() = default;
    D3D9Hook(const D3D9Hook&) = delete;
    D3D9Hook& operator=(const D3D9Hook&) = delete;

    static HRESULT APIENTRY hookedPresent(
        IDirect3DDevice9* device,
        const RECT* srcRect,
        const RECT* dstRect,
        HWND destWindow,
        const RGNDATA* dirtyRegion);

    static HRESULT APIENTRY hookedReset(
        IDirect3DDevice9* device,
        D3DPRESENT_PARAMETERS* params);

    static LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ensureImGuiInitialized(IDirect3DDevice9* device);
    void ensureWndProcSubclassed() noexcept;
    void releaseTextures() noexcept;
    void loadTextures(IDirect3DDevice9* device);
    void loadFonts();

    using PresentFn = HRESULT(APIENTRY*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using ResetFn   = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    static inline PresentFn origPresent_{nullptr};
    static inline ResetFn   origReset_{nullptr};
    static inline WNDPROC   origWndProc_{nullptr};

    void*             presentTarget_{nullptr};
    void*             resetTarget_{nullptr};
    std::atomic<bool> hooksInstalled_{false};
    std::atomic<bool> menuOpenAtomic_{false};
    std::atomic<bool> wndProcPassthroughOnly_{false};
    std::uint64_t     lastWndProcCheckMs_{0};
    bool              imguiInitialized_{false};
    IDirect3DDevice9* boundDevice_{nullptr};
    HWND              boundFocusHwnd_{nullptr};
    HWND              gameHwnd_{nullptr};
    std::atomic<HWND> gameHwndAtomic_{nullptr};
    ImFont*           titleFont_{nullptr};
    ImFont*           badgeFont_{nullptr};
    UiTextures        textures_{};

    FrameCallback     onFrame_{};
    WndProcHook       onWndProc_{};
};

} // namespace svplus::client::ui
