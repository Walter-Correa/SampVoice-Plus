#include "ui/D3D9Hook.hpp"
#include "ShutdownState.hpp"

#include "assets/EmbeddedAssets.hpp"
#include "diag/ModDiagnostics.hpp"
#include "svplus/common/Logger.hpp"

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace svplus::client::ui {
namespace {

IDirect3DTexture9* createTextureFromPng(IDirect3DDevice9* device, std::span<const std::uint8_t> png) noexcept {
    if (!device || png.empty()) {
        return nullptr;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &comp, 4);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return nullptr;
    }

    // Levels = 0 builds the complete mip chain down to 1x1 in D3DPOOL_MANAGED.
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(device->CreateTexture(
            static_cast<UINT>(w), static_cast<UINT>(h), 0, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
        stbi_image_free(rgba);
        return nullptr;
    }

    auto uploadRgbaLevel = [tex](UINT level, int lw, int lh, const std::uint8_t* src) noexcept {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(tex->LockRect(level, &locked, nullptr, 0)) && locked.pBits) {
            for (int y = 0; y < lh; ++y) {
                const std::uint8_t* srcRow = src + static_cast<std::size_t>(y) * lw * 4;
                auto* dstRow = static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
                for (int x = 0; x < lw; ++x) {
                    // Convert RGBA -> BGRA for D3DFMT_A8R8G8B8 little-endian memory layout
                    dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
                    dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
                    dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
                    dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
                }
            }
            tex->UnlockRect(level);
        }
    };

    const std::size_t level0Bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    std::vector<std::uint8_t> currLevel(rgba, rgba + level0Bytes);
    stbi_image_free(rgba);

    uploadRgbaLevel(0, w, h, currLevel.data());

    // CPU 2x2 alpha-weighted downsample avoids dark halo around non-premultiplied transparent edges.
    const UINT levelCount = tex->GetLevelCount();
    int currW = w;
    int currH = h;
    for (UINT level = 1; level < levelCount; ++level) {
        const int nextW = (std::max)(1, currW / 2);
        const int nextH = (std::max)(1, currH / 2);
        std::vector<std::uint8_t> nextLevel(static_cast<std::size_t>(nextW) * static_cast<std::size_t>(nextH) * 4, 0);

        for (int ny = 0; ny < nextH; ++ny) {
            const int y0   = ny * 2;
            const int yEnd = (std::min)(currH, y0 + 2);
            for (int nx = 0; nx < nextW; ++nx) {
                const int x0   = nx * 2;
                const int xEnd = (std::min)(currW, x0 + 2);

                std::uint32_t sumA  = 0;
                std::uint32_t sumR  = 0;
                std::uint32_t sumG  = 0;
                std::uint32_t sumB  = 0;
                std::uint32_t count = 0;

                for (int sy = y0; sy < yEnd; ++sy) {
                    for (int sx = x0; sx < xEnd; ++sx) {
                        const std::size_t idx = (static_cast<std::size_t>(sy) * currW + sx) * 4;
                        const std::uint32_t a = currLevel[idx + 3];
                        sumA += a;
                        sumR += currLevel[idx + 0] * a;
                        sumG += currLevel[idx + 1] * a;
                        sumB += currLevel[idx + 2] * a;
                        ++count;
                    }
                }

                const std::size_t dstIdx = (static_cast<std::size_t>(ny) * nextW + nx) * 4;
                const std::uint8_t outA  = count > 0
                    ? static_cast<std::uint8_t>((sumA + count / 2) / count)
                    : 0;
                nextLevel[dstIdx + 3] = outA;
                if (sumA > 0) {
                    nextLevel[dstIdx + 0] = static_cast<std::uint8_t>((sumR + sumA / 2) / sumA);
                    nextLevel[dstIdx + 1] = static_cast<std::uint8_t>((sumG + sumA / 2) / sumA);
                    nextLevel[dstIdx + 2] = static_cast<std::uint8_t>((sumB + sumA / 2) / sumA);
                }
            }
        }

        uploadRgbaLevel(level, nextW, nextH, nextLevel.data());
        currLevel = std::move(nextLevel);
        currW     = nextW;
        currH     = nextH;
    }

    return tex;
}

void applyModernTheme() noexcept {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 5.0f;
    style.TabRounding       = 5.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(16.0f, 14.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);

    auto* c = style.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.09f, 0.12f, 0.95f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.11f, 0.12f, 0.16f, 0.90f);
    c[ImGuiCol_PopupBg]          = ImVec4(0.10f, 0.11f, 0.15f, 0.98f);
    c[ImGuiCol_Border]           = ImVec4(0.22f, 0.27f, 0.36f, 0.70f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.24f, 0.33f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.24f, 0.30f, 0.42f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.07f, 0.08f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.09f, 0.11f, 0.16f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.22f, 0.74f, 0.97f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.22f, 0.74f, 0.97f, 0.90f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.36f, 0.82f, 1.00f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.16f, 0.42f, 0.68f, 0.85f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.20f, 0.54f, 0.85f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.14f, 0.36f, 0.60f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.18f, 0.46f, 0.74f, 0.65f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.22f, 0.56f, 0.88f, 0.85f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.18f, 0.48f, 0.78f, 1.00f);
    c[ImGuiCol_Tab]              = ImVec4(0.12f, 0.14f, 0.20f, 1.00f);
    c[ImGuiCol_TabHovered]       = ImVec4(0.20f, 0.50f, 0.80f, 0.90f);
    c[ImGuiCol_TabSelected]      = ImVec4(0.18f, 0.46f, 0.75f, 1.00f);
    c[ImGuiCol_PlotHistogram]    = ImVec4(0.22f, 0.80f, 0.55f, 1.00f);
}

constexpr std::uintptr_t kGtaD3d9DevicePtrAddr = 0xC97C28u; // GTA SA 1.0 US RwD3D9Device

bool isPlausibleWin32Ptr(const void* p) noexcept {
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    return addr >= 0x10000u && addr < 0x7FFF0000u;
}

bool tryGetVtablesFromDummyDevice(void*& outReset, void*& outPresent, HRESULT& outHr) noexcept {
    outReset   = nullptr;
    outPresent = nullptr;
    outHr      = E_FAIL;

    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SvPlusDummyD3D9Class";
    RegisterClassExA(&wc);

    HWND dummyHwnd = CreateWindowExA(
        0, wc.lpszClassName, "SvPlusDummy", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9 || !dummyHwnd) {
        if (d3d9) d3d9->Release();
        if (dummyHwnd) DestroyWindow(dummyHwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed      = TRUE;
    pp.SwapEffect    = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = dummyHwnd;

    IDirect3DDevice9* dummyDev = nullptr;
    HRESULT hr = d3d9->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyHwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
        &pp, &dummyDev);
    if (FAILED(hr) || !dummyDev) {
        hr = d3d9->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, dummyHwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummyDev);
    }
    outHr = hr;

    bool ok = false;
    if (SUCCEEDED(hr) && dummyDev) {
        void** vtable = *reinterpret_cast<void***>(dummyDev);
        if (vtable && isPlausibleWin32Ptr(vtable[16]) && isPlausibleWin32Ptr(vtable[17])) {
            outReset   = vtable[16];
            outPresent = vtable[17];
            ok = true;
        }
        dummyDev->Release();
    }

    d3d9->Release();
    DestroyWindow(dummyHwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return ok;
}

bool isExecutableModuleAddress(const void* addr) noexcept {
    if (!isPlausibleWin32Ptr(addr)) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
        return false;
    }
    constexpr DWORD kExecMask =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & kExecMask) == 0 || (mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    HMODULE ownerMod = nullptr;
    return GetModuleHandleExW(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(addr),
               &ownerMod) != FALSE &&
           ownerMod != nullptr;
}

// Isolated SEH fallback when dummy device creation fails: borrows GTA SA 1.0 US global IDirect3DDevice9*.
bool tryGetVtablesFromGtaDevice(void*& outReset, void*& outPresent) noexcept {
    outReset   = nullptr;
    outPresent = nullptr;
    void* candidateReset   = nullptr;
    void* candidatePresent = nullptr;
    __try {
        IDirect3DDevice9* const gameDev = *reinterpret_cast<IDirect3DDevice9* const*>(kGtaD3d9DevicePtrAddr);
        if (!isPlausibleWin32Ptr(gameDev)) {
            return false;
        }
        void** vtable = *reinterpret_cast<void***>(gameDev);
        if (!isPlausibleWin32Ptr(vtable) || !isPlausibleWin32Ptr(vtable[16]) || !isPlausibleWin32Ptr(vtable[17])) {
            return false;
        }
        candidateReset   = vtable[16];
        candidatePresent = vtable[17];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (!isExecutableModuleAddress(candidateReset) || !isExecutableModuleAddress(candidatePresent)) {
        return false;
    }
    outReset   = candidateReset;
    outPresent = candidatePresent;
    return true;
}

bool tryCopyCodeBytes(const void* src, std::uint8_t* dst, std::size_t len) noexcept {
    if (!src || !dst || len == 0) {
        return false;
    }
    __try {
        std::memcpy(dst, src, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

D3D9Hook& D3D9Hook::instance() noexcept {
    static auto* inst = new D3D9Hook();
    return *inst;
}

bool D3D9Hook::initialize(FrameCallback onFrame, WndProcHook onWndProc, bool suppressRetryLog) {
    if (hooksInstalled_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!onFrame_) {
        onFrame_ = std::move(onFrame);
    }
    if (!onWndProc_) {
        onWndProc_ = std::move(onWndProc);
    }

    HRESULT dummyHr = E_FAIL;
    void* resetPtr   = nullptr;
    void* presentPtr = nullptr;
    const char* sourceName = "dummy D3D9 device";

    if (!tryGetVtablesFromDummyDevice(resetPtr, presentPtr, dummyHr)) {
        if (tryGetVtablesFromGtaDevice(resetPtr, presentPtr)) {
            sourceName = "GTA SA device (0xC97C28)";
            SVPLUS_LOG_WRN_CAT("render", "Dummy D3D9 CreateDevice failed (0x%08lX); falling back to %s",
                               dummyHr, sourceName);
        } else {
            if (!suppressRetryLog) {
                SVPLUS_LOG_WRN_CAT("render", "Dummy D3D9 CreateDevice failed (0x%08lX) and GTA device pointer not ready yet",
                                   dummyHr);
            }
            return false;
        }
    }

    resetTarget_   = resetPtr;
    presentTarget_ = presentPtr;

    std::uint8_t presentBefore[12]{};
    std::uint8_t resetBefore[12]{};
    const bool hasPresentBefore = tryCopyCodeBytes(presentTarget_, presentBefore, sizeof(presentBefore));
    const bool hasResetBefore   = tryCopyCodeBytes(resetTarget_, resetBefore, sizeof(resetBefore));

    if (MH_CreateHook(presentTarget_, reinterpret_cast<void*>(&D3D9Hook::hookedPresent),
                      reinterpret_cast<void**>(&origPresent_)) != MH_OK) {
        SVPLUS_LOG_WRN_CAT("render", "Failed to hook IDirect3DDevice9::Present");
        return false;
    }
    if (MH_CreateHook(resetTarget_, reinterpret_cast<void*>(&D3D9Hook::hookedReset),
                      reinterpret_cast<void**>(&origReset_)) != MH_OK) {
        MH_RemoveHook(presentTarget_);
        SVPLUS_LOG_WRN_CAT("render", "Failed to hook IDirect3DDevice9::Reset");
        return false;
    }

    MH_EnableHook(presentTarget_);
    MH_EnableHook(resetTarget_);
    hooksInstalled_.store(true, std::memory_order_release);
    SVPLUS_LOG_INF_CAT("render", "IDirect3DDevice9 Present/Reset hooks installed via %s", sourceName);
    diag::reportD3D9HookInspection("Present", presentTarget_,
                                   hasPresentBefore ? presentBefore : nullptr,
                                   hasPresentBefore ? sizeof(presentBefore) : 0);
    diag::reportD3D9HookInspection("Reset", resetTarget_,
                                   hasResetBefore ? resetBefore : nullptr,
                                   hasResetBefore ? sizeof(resetBefore) : 0);
    return true;
}

void D3D9Hook::loadFonts() {
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    titleFont_ = nullptr;
    badgeFont_ = nullptr;

    static const ImWchar mainRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement (DE, ES, FR, IT, PT, ID)
        0x0100, 0x024F, // Latin Extended-A/B (HU, PL, RO, TR, VI base)
        0x0300, 0x0323, // Combining Diacritical Marks (Vietnamese stacked accents)
        0x0400, 0x052F, // Cyrillic + Supplement (BG, RU, UK)
        0x1E00, 0x1EFF, // Latin Extended Additional (Vietnamese stacked vowels)
        0x2010, 0x2027, // Hyphens, dashes (em-dash U+2014), typographic quotes, ellipsis
        0x20AC, 0x20AC, // Euro sign
        0x2122, 0x2122, // Trade mark sign
        0,
    };

    const auto fontBytes = assets::kFontTtf();
    ImFontConfig cfg{};
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH          = 2;
    cfg.OversampleV          = 2;
    io.Fonts->AddFontFromMemoryTTF(
        const_cast<std::uint8_t*>(fontBytes.data()),
        static_cast<int>(fontBytes.size()),
        17.0f,
        &cfg,
        mainRanges);

    const auto titleBytes = assets::kTitleFontTtf();
    if (!titleBytes.empty()) {
        ImFontConfig titleCfg{};
        titleCfg.FontDataOwnedByAtlas = false;
        titleCfg.OversampleH          = 2;
        titleCfg.OversampleV          = 2;
        titleFont_ = io.Fonts->AddFontFromMemoryTTF(
            const_cast<std::uint8_t*>(titleBytes.data()),
            static_cast<int>(titleBytes.size()),
            26.0f,
            &titleCfg,
            io.Fonts->GetGlyphRangesDefault());

        ImFontConfig badgeCfg{};
        badgeCfg.FontDataOwnedByAtlas = false;
        badgeCfg.OversampleH          = 2;
        badgeCfg.OversampleV          = 2;
        badgeFont_ = io.Fonts->AddFontFromMemoryTTF(
            const_cast<std::uint8_t*>(titleBytes.data()),
            static_cast<int>(titleBytes.size()),
            12.0f,
            &badgeCfg,
            io.Fonts->GetGlyphRangesDefault());
    }
}

void D3D9Hook::loadTextures(IDirect3DDevice9* device) {
    releaseTextures();
    textures_.microActive  = createTextureFromPng(device, assets::kMicroActivePng());
    textures_.microMuted   = createTextureFromPng(device, assets::kMicroMutedPng());
    textures_.microPassive = createTextureFromPng(device, assets::kMicroPassivePng());
    textures_.speakerDeaf  = createTextureFromPng(device, assets::kSpeakerDeafPng());
    textures_.microMissing = createTextureFromPng(device, assets::kMicroMissingPng());

    const std::array<std::span<const std::uint8_t>, protocol::kStreamIconCount> iconAssets = {
        assets::kSpeakerPng(),
        assets::kRadioPng(),
        assets::kPhonePng(),
        assets::kMegaphonePng(),
        assets::kPolicePng(),
        assets::kAdminPng(),
        assets::kGlobalPng(),
    };
    const std::array<std::span<const std::uint8_t>, protocol::kStreamIconCount> blockedIconAssets = {
        assets::kSpeakerBlockedPng(),
        assets::kRadioBlockedPng(),
        assets::kPhoneBlockedPng(),
        assets::kMegaphoneBlockedPng(),
        assets::kPoliceBlockedPng(),
        assets::kAdminBlockedPng(),
        assets::kGlobalBlockedPng(),
    };
    for (std::size_t i = 0; i < iconAssets.size(); ++i) {
        textures_.streamIcons[i]        = createTextureFromPng(device, iconAssets[i]);
        textures_.streamIconsBlocked[i] = createTextureFromPng(device, blockedIconAssets[i]);
    }
    textures_.speaker        = textures_.streamIcons[static_cast<std::size_t>(protocol::StreamIconId::Speaker)];
    textures_.speakerBlocked = textures_.streamIconsBlocked[static_cast<std::size_t>(protocol::StreamIconId::Speaker)];
}

void D3D9Hook::releaseTextures() noexcept {
    auto safeRelease = [](IDirect3DTexture9*& tex) noexcept {
        if (tex) {
            tex->Release();
            tex = nullptr;
        }
    };
    safeRelease(textures_.microActive);
    safeRelease(textures_.microMuted);
    safeRelease(textures_.microPassive);
    safeRelease(textures_.speakerDeaf);
    safeRelease(textures_.microMissing);
    for (auto& tex : textures_.streamIcons) {
        safeRelease(tex);
    }
    for (auto& tex : textures_.streamIconsBlocked) {
        safeRelease(tex);
    }
    textures_.speaker        = nullptr;
    textures_.speakerBlocked = nullptr;
}

void D3D9Hook::ensureImGuiInitialized(IDirect3DDevice9* device) {
    if (!device) {
        return;
    }

    D3DDEVICE_CREATION_PARAMETERS params{};
    if (FAILED(device->GetCreationParameters(&params)) || !params.hFocusWindow) {
        return;
    }

    // Bind strictly to the GTA:SA focus window (captured on first matching D3D9 device) so
    // secondary helper devices created by ENB, overlays, or CEF/browsers are ignored and do not
    // draw or thrash ImGui state.
    if (!boundFocusHwnd_) {
        char className[128]{};
        const int len = GetClassNameA(
            params.hFocusWindow, className, static_cast<int>(sizeof(className)));
        if (len <= 0 || std::strcmp(className, "Grand theft auto San Andreas") != 0) {
            return;
        }
        boundFocusHwnd_ = params.hFocusWindow;
        SVPLUS_LOG_INF_CAT("render", "D3D9Hook bound to game window 0x%p (class='%s')",
                           static_cast<void*>(params.hFocusWindow), className);
    } else if (params.hFocusWindow != boundFocusHwnd_) {
        return;
    }
    if (imguiInitialized_ && boundDevice_ == device) {
        return;
    }

    if (imguiInitialized_) {
        releaseTextures();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }

    gameHwnd_    = boundFocusHwnd_;
    gameHwndAtomic_.store(gameHwnd_, std::memory_order_release);
    boundDevice_ = device;

    ensureWndProcSubclassed();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;

    applyModernTheme();
    loadFonts();

    ImGui_ImplWin32_Init(gameHwnd_);
    ImGui_ImplDX9_Init(device);
    loadTextures(device);
    imguiInitialized_ = true;
}

void D3D9Hook::ensureWndProcSubclassed() noexcept {
    if (!origWndProc_ && gameHwnd_) {
        origWndProc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongA(gameHwnd_, GWL_WNDPROC, reinterpret_cast<LONG>(&D3D9Hook::hookedWndProc)));
        wndProcPassthroughOnly_.store(false, std::memory_order_release);
        diag::reportWndProcAttach(gameHwnd_, origWndProc_, &D3D9Hook::hookedWndProc);
    }
}

void D3D9Hook::unsubclassWndProc() noexcept {
    if (origWndProc_ && gameHwnd_) {
        const LONG currentProc = GetWindowLongA(gameHwnd_, GWL_WNDPROC);
        if (currentProc == reinterpret_cast<LONG>(&D3D9Hook::hookedWndProc)) {
            WNDPROC restored = origWndProc_;
            SetWindowLongA(gameHwnd_, GWL_WNDPROC, reinterpret_cast<LONG>(restored));
            origWndProc_ = nullptr;
            wndProcPassthroughOnly_.store(false, std::memory_order_release);
            SVPLUS_LOG_INF_CAT("render", "Restored original WndProc 0x%p on unsubclass",
                               reinterpret_cast<void*>(restored));
        } else {
            // Another mod wrapped GWL_WNDPROC after us; overwriting it would break its chain.
            wndProcPassthroughOnly_.store(true, std::memory_order_release);
            SVPLUS_LOG_WRN_CAT(
                "render",
                "GWL_WNDPROC (0x%08lX) replaced by another module after attach; "
                "preserving chain in passthrough mode (origWndProc=0x%p)",
                static_cast<unsigned long>(currentProc),
                reinterpret_cast<void*>(origWndProc_));
        }
    }
}

void D3D9Hook::setMenuOpenState(bool open) noexcept {
    const bool prev = menuOpenAtomic_.exchange(open, std::memory_order_acq_rel);
    if (prev && !open) {
        if (imguiInitialized_ && ImGui::GetCurrentContext()) {
            // Release any active Win32 capture/button mask and reset ImGui mouse/focus flags so
            // game/singleplayer menus never inherit stuck drag or button state on close.
            if (gameHwnd_) {
                ImGui_ImplWin32_WndProcHandler(gameHwnd_, WM_LBUTTONUP, 0, 0);
                ImGui_ImplWin32_WndProcHandler(gameHwnd_, WM_RBUTTONUP, 0, 0);
                ImGui_ImplWin32_WndProcHandler(gameHwnd_, WM_MBUTTONUP, 0, 0);
            }
            auto& io = ImGui::GetIO();
            for (int btn = 0; btn < ImGuiMouseButton_COUNT; ++btn) {
                io.AddMouseButtonEvent(btn, false);
            }
            io.AddFocusEvent(false);
        }
        if (gameHwnd_ && GetCapture() == gameHwnd_) {
            ReleaseCapture();
        }
    }
}

HRESULT APIENTRY D3D9Hook::hookedPresent(
    IDirect3DDevice9* device,
    const RECT* srcRect,
    const RECT* dstRect,
    HWND destWindow,
    const RGNDATA* dirtyRegion) {
    auto& self = instance();
    if (device && !isProcessShuttingDown() && isSampModuleLoaded()) {
        self.ensureImGuiInitialized(device);
        self.ensureWndProcSubclassed();

        const std::uint64_t nowMs = GetTickCount64();
        if (self.gameHwnd_ && (nowMs - self.lastWndProcCheckMs_) >= 2000u) {
            self.lastWndProcCheckMs_ = nowMs;
            diag::checkWndProcChange(self.gameHwnd_, &D3D9Hook::hookedWndProc);
        }

        if (self.imguiInitialized_ && self.boundDevice_ == device) {
            auto& io = ImGui::GetIO();
            if (self.menuOpenAtomic_.load(std::memory_order_acquire)) {
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            } else {
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            }
            io.MouseDrawCursor = false;

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (self.onFrame_) {
                self.onFrame_(device, self.gameHwnd_, self.textures_);
            }

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
    }
    return origPresent_ ? origPresent_(device, srcRect, dstRect, destWindow, dirtyRegion) : D3D_OK;
}

HRESULT APIENTRY D3D9Hook::hookedReset(
    IDirect3DDevice9* device,
    D3DPRESENT_PARAMETERS* params) {
    auto& self = instance();
    const bool isBoundDevice = self.imguiInitialized_ && (device == self.boundDevice_);
    if (isBoundDevice) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    const HRESULT hr = origReset_ ? origReset_(device, params) : D3D_OK;
    if (SUCCEEDED(hr) && isBoundDevice) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

LRESULT CALLBACK D3D9Hook::hookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& self = instance();
    auto passThrough = [&]() noexcept -> LRESULT {
        return origWndProc_
            ? CallWindowProcA(origWndProc_, hwnd, msg, wParam, lParam)
            : DefWindowProcA(hwnd, msg, wParam, lParam);
    };
    if (self.wndProcPassthroughOnly_.load(std::memory_order_acquire) || !isSampModuleLoaded()) {
        return passThrough();
    }
    if (self.imguiInitialized_ && self.menuOpenAtomic_.load(std::memory_order_acquire)) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
    }
    if (self.onWndProc_ && self.onWndProc_(hwnd, msg, wParam, lParam)) {
        return TRUE;
    }
    return passThrough();
}

void D3D9Hook::shutdown() noexcept {
    if (isProcessShuttingDown()) {
        return;
    }
    unsubclassWndProc();
    if (imguiInitialized_) {
        releaseTextures();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (hooksInstalled_.exchange(false, std::memory_order_acq_rel)) {
        if (presentTarget_) {
            MH_DisableHook(presentTarget_);
            MH_RemoveHook(presentTarget_);
        }
        if (resetTarget_) {
            MH_DisableHook(resetTarget_);
            MH_RemoveHook(resetTarget_);
        }
    }
    gameHwndAtomic_.store(nullptr, std::memory_order_release);
}

} // namespace svplus::client::ui
