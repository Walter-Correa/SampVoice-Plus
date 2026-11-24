#include "diag/ModDiagnostics.hpp"

#include "ShutdownState.hpp"
#include <svplus/common/Logger.hpp>
#include <svplus/common/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace svplus::client::diag {
namespace {

constexpr const char* kCleanGtaSa10UsSha256 =
    "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26";
constexpr const char* kCleanDinput8Sha256 =
    "d7865fbe8319d993b8eeff094593cb70bea3e49f02095a308605e14f11a34353";
constexpr const char* kCleanVorbisFileSha256 =
    "a08923479000cec366967fb8259e0920b7aa18859722c7dda1415726bed4774f";

std::string wideToUtf8(std::wstring_view ws) {
    if (ws.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return text.size() == prefix.size() ||
           text[prefix.size()] == '\\' ||
           text[prefix.size()] == '/';
}

std::string userProfileUtf8() {
    wchar_t buf[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return wideToUtf8(std::wstring_view(buf, len));
    }
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return wideToUtf8(buf);
    }
    return {};
}

std::string systemRootUtf8() {
    wchar_t buf[MAX_PATH]{};
    const UINT len = GetWindowsDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return wideToUtf8(std::wstring_view(buf, len));
    }
    return {};
}

bool isUnderSystemRoot(const std::filesystem::path& p) {
    const std::string sysRoot = systemRootUtf8();
    if (sysRoot.empty()) {
        return false;
    }
    const std::string u = wideToUtf8(p.wstring());
    return startsWithNoCase(u, sysRoot);
}

bool isProxyDllName(std::string_view filenameLower) {
    return filenameLower == "d3d9.dll" ||
           filenameLower == "dinput8.dll" ||
           filenameLower == "dsound.dll" ||
           filenameLower == "winmm.dll" ||
           filenameLower == "opengl32.dll" ||
           filenameLower == "ddraw.dll" ||
           filenameLower == "bass.dll";
}

bool containsAsciiNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

bool isSamePathNoCase(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    std::error_code ec;
    const auto ca = std::filesystem::weakly_canonical(a, ec);
    const auto cb = std::filesystem::weakly_canonical(b, ec);
    const std::string sa = toLowerAscii(wideToUtf8((ca.empty() ? a : ca).wstring()));
    const std::string sb = toLowerAscii(wideToUtf8((cb.empty() ? b : cb).wstring()));
    return !sa.empty() && sa == sb;
}

const char* identifyProbableModName(std::string_view lowerName) noexcept {
    if (lowerName == "modloader.asi") return "ModLoader (probable)";
    if (lowerName == "sampfuncs.asi") return "SAMPFUNCS (probable)";
    if (lowerName == "cleo.asi") return "CLEO (probable)";
    if (lowerName == "moonloader.asi") return "MoonLoader (probable)";
    if (lowerName == "silentpatchsa.asi") return "SilentPatch SA (probable)";
    if (lowerName == "mixsets.asi") return "MixSets (probable)";
    if (lowerName == "crashes.asi") return "Crashes.asi (probable)";
    if (lowerName == "fpsunlock.asi") return "FPSUnlock (probable)";
    if (lowerName == "imvehft.asi") return "Improved Vehicle Features (probable)";
    if (lowerName == "widescreenfix.asi" || lowerName == "gtasa.widescreenfix.asi") {
        return "Widescreen Fix (probable)";
    }
    if (lowerName == "fastman92limitadjuster.asi" || lowerName == "iii.vc.sa.limitadjuster.asi") {
        return "Limit Adjuster (probable)";
    }
    if (lowerName == "crashinfo.asi" || lowerName == "scrlog.asi") return "Crash Logger (probable)";
    if (lowerName == "mousefix.asi" || lowerName == "flicker.asi") return "GTA Mouse/Flicker Fix (probable)";
    if (lowerName == "outfit.asi" || lowerName == "skygrad.asi" || lowerName == "skygfx.asi") {
        return "Graphics Tweaker (probable)";
    }
    if (lowerName == "streammemfix.asi" || lowerName == "memory512.asi") {
        return "Stream Memory Fix (probable)";
    }
    return nullptr;
}

std::string computeFileSha256(const std::filesystem::path& filePath, std::uint64_t* outSize = nullptr) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER li{};
    if (GetFileSizeEx(hFile, &li) && outSize) {
        *outSize = static_cast<std::uint64_t>(li.QuadPart);
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cbData = 0;
        if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                              sizeof(objLen), &cbData, 0) == 0 && objLen > 0) {
            std::vector<UCHAR> hashObj(objLen);
            if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0) {
                std::array<UCHAR, 65536> buf{};
                DWORD bytesRead = 0;
                bool ok = true;
                while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) &&
                       bytesRead > 0) {
                    if (BCryptHashData(hHash, buf.data(), bytesRead, 0) != 0) {
                        ok = false;
                        break;
                    }
                }
                UCHAR digest[32]{};
                if (ok && BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    hex.reserve(64);
                    for (UCHAR b : digest) {
                        hex.push_back(kHex[(b >> 4) & 0xF]);
                        hex.push_back(kHex[b & 0xF]);
                    }
                }
                BCryptDestroyHash(hHash);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    CloseHandle(hFile);
    return hex;
}

struct FileVersionMeta {
    bool        hasVersionInfo{false};
    std::string fileVersion;
    std::string productName;
    std::string fileDescription;
};

FileVersionMeta readFileVersionMeta(const std::filesystem::path& filePath) {
    FileVersionMeta meta{};
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &handle);
    if (size == 0) {
        return meta;
    }
    std::vector<std::uint8_t> data(size);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, size, data.data())) {
        return meta;
    }
    meta.hasVersionInfo = true;

    struct LangCodePage { WORD lang; WORD codePage; };
    LangCodePage* trans = nullptr;
    UINT transLen = 0;
    auto queryString = [&](WORD lang, WORD cp, const wchar_t* field) -> std::string {
        wchar_t subBlock[128]{};
        _snwprintf_s(subBlock, _countof(subBlock), _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s", lang, cp, field);
        wchar_t* val = nullptr;
        UINT valLen = 0;
        if (VerQueryValueW(data.data(), subBlock, reinterpret_cast<LPVOID*>(&val), &valLen) && val && valLen > 0) {
            return wideToUtf8(val);
        }
        return {};
    };

    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID*>(&trans), &transLen) &&
        trans && transLen >= sizeof(LangCodePage)) {
        meta.fileVersion     = queryString(trans[0].lang, trans[0].codePage, L"FileVersion");
        meta.productName     = queryString(trans[0].lang, trans[0].codePage, L"ProductName");
        meta.fileDescription = queryString(trans[0].lang, trans[0].codePage, L"FileDescription");
    }
    if (meta.fileVersion.empty()) {
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT ffiLen = 0;
        if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen) &&
            ffi && ffiLen >= sizeof(VS_FIXEDFILEINFO)) {
            char ver[64];
            std::snprintf(ver, sizeof(ver), "%u.%u.%u.%u",
                          HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                          HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
            meta.fileVersion = ver;
        }
    }
    return meta;
}

std::uint32_t rvaToFileOffset(
    std::uint32_t               rva,
    const IMAGE_SECTION_HEADER* sections,
    WORD                        numSections) noexcept {
    for (WORD i = 0; i < numSections; ++i) {
        const std::uint32_t va = sections[i].VirtualAddress;
        const std::uint32_t span = (std::max)(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData);
        if (rva >= va && rva < va + span && sections[i].PointerToRawData > 0) {
            const std::uint32_t delta = rva - va;
            if (delta < sections[i].SizeOfRawData) {
                return sections[i].PointerToRawData + delta;
            }
        }
    }
    return 0;
}

bool readFileSlice(HANDLE hFile, std::uint64_t offset, void* out, DWORD len, DWORD* outRead = nullptr) noexcept {
    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN) == FALSE) {
        return false;
    }
    DWORD got = 0;
    if (ReadFile(hFile, out, len, &got, nullptr) == FALSE) {
        return false;
    }
    if (outRead) {
        *outRead = got;
    }
    return got > 0;
}

bool peExportsContainAny(const std::filesystem::path& filePath, std::initializer_list<const char*> targets) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool matched = false;
    IMAGE_DOS_HEADER dos{};
    DWORD got = 0;
    if (readFileSlice(hFile, 0, &dos, sizeof(dos), &got) &&
        got == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 0) {
        IMAGE_NT_HEADERS32 nt{};
        if (readFileSlice(hFile, static_cast<std::uint64_t>(dos.e_lfanew), &nt, sizeof(nt), &got) &&
            got == sizeof(nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt.FileHeader.NumberOfSections > 0 && nt.FileHeader.NumberOfSections <= 64) {
            const std::uint64_t secOff =
                static_cast<std::uint64_t>(dos.e_lfanew) +
                offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
                nt.FileHeader.SizeOfOptionalHeader;
            std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
            const DWORD secBytes = static_cast<DWORD>(sections.size() * sizeof(IMAGE_SECTION_HEADER));
            if (readFileSlice(hFile, secOff, sections.data(), secBytes, &got) && got == secBytes) {
                const auto& expDirEntry = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                if (expDirEntry.VirtualAddress != 0 && expDirEntry.Size >= sizeof(IMAGE_EXPORT_DIRECTORY)) {
                    const std::uint32_t expRaw = rvaToFileOffset(
                        expDirEntry.VirtualAddress, sections.data(), nt.FileHeader.NumberOfSections);
                    IMAGE_EXPORT_DIRECTORY expDir{};
                    if (expRaw > 0 && readFileSlice(hFile, expRaw, &expDir, sizeof(expDir), &got) &&
                        got == sizeof(expDir) && expDir.NumberOfNames > 0 && expDir.AddressOfNames != 0) {
                        const std::uint32_t namesRaw = rvaToFileOffset(
                            expDir.AddressOfNames, sections.data(), nt.FileHeader.NumberOfSections);
                        const DWORD count = (std::min<DWORD>)(expDir.NumberOfNames, 2048u);
                        std::vector<std::uint32_t> nameRvas(count);
                        if (namesRaw > 0 &&
                            readFileSlice(hFile, namesRaw, nameRvas.data(), count * sizeof(std::uint32_t), &got)) {
                            const DWORD gotCount = got / sizeof(std::uint32_t);
                            char nameBuf[128]{};
                            for (DWORD i = 0; i < gotCount && !matched; ++i) {
                                const std::uint32_t nRaw = rvaToFileOffset(
                                    nameRvas[i], sections.data(), nt.FileHeader.NumberOfSections);
                                if (nRaw > 0 && readFileSlice(hFile, nRaw, nameBuf, sizeof(nameBuf) - 1, &got)) {
                                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                                    for (const char* t : targets) {
                                        if (std::strcmp(nameBuf, t) == 0) {
                                            matched = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    CloseHandle(hFile);
    return matched;
}

bool peReadOnlySectionsContainAny(
    const std::filesystem::path& filePath,
    std::initializer_list<std::string_view> markers) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool matched = false;
    IMAGE_DOS_HEADER dos{};
    DWORD got = 0;
    if (readFileSlice(hFile, 0, &dos, sizeof(dos), &got) &&
        got == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 0) {
        IMAGE_NT_HEADERS32 nt{};
        if (readFileSlice(hFile, static_cast<std::uint64_t>(dos.e_lfanew), &nt, sizeof(nt), &got) &&
            got == sizeof(nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt.FileHeader.NumberOfSections > 0 && nt.FileHeader.NumberOfSections <= 64) {
            const std::uint64_t secOff =
                static_cast<std::uint64_t>(dos.e_lfanew) +
                offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
                nt.FileHeader.SizeOfOptionalHeader;
            std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
            const DWORD secBytes = static_cast<DWORD>(sections.size() * sizeof(IMAGE_SECTION_HEADER));
            if (readFileSlice(hFile, secOff, sections.data(), secBytes, &got) && got == secBytes) {
                for (const auto& sec : sections) {
                    const bool isRead  = (sec.Characteristics & IMAGE_SCN_MEM_READ) != 0;
                    const bool isWrite = (sec.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
                    const bool isExec  = (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
                    if (!isRead || isWrite || isExec || sec.PointerToRawData == 0 || sec.SizeOfRawData == 0) {
                        continue;
                    }
                    const DWORD toRead = (std::min<DWORD>)(sec.SizeOfRawData, 4u * 1024u * 1024u);
                    std::string buf(toRead, '\0');
                    if (readFileSlice(hFile, sec.PointerToRawData, buf.data(), toRead, &got)) {
                        buf.resize(got);
                        for (std::string_view m : markers) {
                            if (buf.find(m) != std::string::npos) {
                                matched = true;
                                break;
                            }
                        }
                    }
                    if (matched) break;
                }
            }
        }
    }

    CloseHandle(hFile);
    return matched;
}

bool isAsiLoaderDll(
    const std::filesystem::path& filePath,
    std::string_view             lowerName,
    std::string_view             sha256,
    const FileVersionMeta&       meta) {
    const bool hasLoaderMeta =
        containsAsciiNoCase(meta.productName, "ASI Loader") ||
        containsAsciiNoCase(meta.productName, "Ultimate-ASI-Loader") ||
        containsAsciiNoCase(meta.fileDescription, "ASI Loader") ||
        containsAsciiNoCase(meta.fileDescription, "Ultimate ASI Loader");
    if (lowerName == "vorbisfile.dll") {
        if (sha256 == kCleanVorbisFileSha256) {
            return false;
        }
        return hasLoaderMeta ||
               peExportsContainAny(filePath, {"ov_read", "ov_open_callbacks", "ov_clear"});
    }
    if (lowerName == "dinput8.dll") {
        if (sha256 == kCleanDinput8Sha256) {
            return false;
        }
        return hasLoaderMeta ||
               peExportsContainAny(filePath, {"DirectInput8Create", "AddVirtualFileForOverloadW"});
    }
    return hasLoaderMeta;
}

bool isOtherVoiceClientAsi(
    const std::filesystem::path& filePath,
    std::string_view             lowerName,
    const std::filesystem::path& selfModulePath) {
    if (!selfModulePath.empty() && isSamePathNoCase(filePath, selfModulePath)) {
        return false;
    }
    if (lowerName.find("sampvoice") != std::string_view::npos) {
        return true;
    }
    return peReadOnlySectionsContainAny(
        filePath, {"[sv:dbg:", "[sv:err:", "Initializing SA-MP ", "SampVoice Plus Client"});
}

std::string formatModuleAndOffset(const void* addr) {
    if (!addr) {
        return "0x00000000";
    }
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(addr), &mod) && mod) {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(mod, path, MAX_PATH) > 0) {
            const std::string base = wideToUtf8(std::filesystem::path(path).filename().wstring());
            const auto off = reinterpret_cast<std::uintptr_t>(addr) - reinterpret_cast<std::uintptr_t>(mod);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s+0x%X", base.c_str(), static_cast<unsigned>(off));
            return buf;
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%p", addr);
    return buf;
}

bool readProcessBytesSafe(const void* addr, std::uint8_t* out, std::size_t len, const void** outDetourTarget) noexcept {
    if (!addr || !out || len == 0) {
        return false;
    }
    *outDetourTarget = nullptr;
    __try {
        std::memcpy(out, addr, len);
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(addr);
        if (len >= 5 && out[0] == 0xE9) {
            std::int32_t rel = 0;
            std::memcpy(&rel, out + 1, 4);
            *outDetourTarget = reinterpret_cast<const void*>(base + 5 + static_cast<std::intptr_t>(rel));
        } else if (len >= 2 && out[0] == 0xEB) {
            const auto rel = static_cast<std::int8_t>(out[1]);
            *outDetourTarget = reinterpret_cast<const void*>(base + 2 + static_cast<std::intptr_t>(rel));
        } else if (len >= 6 && out[0] == 0xFF && out[1] == 0x25) {
            std::uintptr_t ptrAddr = 0;
            std::memcpy(&ptrAddr, out + 2, 4);
            if (ptrAddr >= 0x10000u) {
                *outDetourTarget = *reinterpret_cast<const void* const*>(ptrAddr);
            }
        } else if (len >= 6 && out[0] == 0x68 && out[5] == 0xC3) {
            std::uintptr_t pushAddr = 0;
            std::memcpy(&pushAddr, out + 1, 4);
            *outDetourTarget = reinterpret_cast<const void*>(pushAddr);
        } else if (len >= 7 && out[0] == 0xB8 && out[5] == 0xFF && out[6] == 0xE0) {
            std::uintptr_t movAddr = 0;
            std::memcpy(&movAddr, out + 1, 4);
            *outDetourTarget = reinterpret_cast<const void*>(movAddr);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string formatBytesHex(const std::uint8_t* bytes, std::size_t len) {
    std::string out;
    out.reserve(len * 3);
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < len; ++i) {
        if (i > 0) out.push_back(' ');
        out.push_back(kHex[(bytes[i] >> 4) & 0xF]);
        out.push_back(kHex[bytes[i] & 0xF]);
    }
    return out;
}

// ============================================================================
// Dynamic LdrRegisterDllNotification queue (loader-lock safe!)
// ============================================================================

struct DllNotifyEvent {
    bool         loaded{false};
    std::wstring fullPath{};
    void*        base{nullptr};
    std::uint32_t size{0};
};

struct DiagnosticsState {
    std::mutex               gtaMutex;
    std::filesystem::path    gtaDir;
    std::string              selfPath;

    CRITICAL_SECTION         queueCs{};
    bool                     queueCsInit{false};
    std::vector<DllNotifyEvent> events;

    PVOID                    ldrCookie{nullptr};
    std::atomic<WNDPROC>     lastObservedWndProc{nullptr};
    std::atomic<bool>        running{false};
    std::thread*             worker{nullptr};
};

DiagnosticsState& diagState() noexcept {
    static auto* s = new DiagnosticsState();
    return *s;
}

struct LDR_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};

struct LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG                     Flags;
    const LDR_UNICODE_STRING* FullDllName;
    const LDR_UNICODE_STRING* BaseDllName;
    PVOID                     DllBase;
    ULONG                     SizeOfImage;
};

struct LDR_DLL_UNLOADED_NOTIFICATION_DATA {
    ULONG                     Flags;
    const LDR_UNICODE_STRING* FullDllName;
    const LDR_UNICODE_STRING* BaseDllName;
    PVOID                     DllBase;
    ULONG                     SizeOfImage;
};

union LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA   Loaded;
    LDR_DLL_UNLOADED_NOTIFICATION_DATA Unloaded;
};

using PLDR_DLL_NOTIFICATION_FUNCTION = VOID(CALLBACK*)(
    ULONG NotificationReason,
    const LDR_DLL_NOTIFICATION_DATA* NotificationData,
    PVOID Context);

using LdrRegisterDllNotificationFn = LONG(NTAPI*)(
    ULONG Flags,
    PLDR_DLL_NOTIFICATION_FUNCTION NotificationFunction,
    PVOID Context,
    PVOID* Cookie);

using LdrUnregisterDllNotificationFn = LONG(NTAPI*)(PVOID Cookie);

constexpr ULONG kLdrDllLoadedReason   = 1;
constexpr ULONG kLdrDllUnloadedReason = 2;

VOID CALLBACK onLdrDllNotification(
    ULONG reason,
    const LDR_DLL_NOTIFICATION_DATA* data,
    PVOID) noexcept {
    auto& st = diagState();
    if (!st.queueCsInit || !data || isProcessShuttingDown()) {
        return;
    }
    const LDR_UNICODE_STRING* name =
        (reason == kLdrDllLoadedReason) ? data->Loaded.FullDllName : data->Unloaded.FullDllName;
    if (!name || !name->Buffer || name->Length == 0) {
        return;
    }
    DllNotifyEvent ev{};
    ev.loaded = (reason == kLdrDllLoadedReason);
    ev.base   = (reason == kLdrDllLoadedReason) ? data->Loaded.DllBase : data->Unloaded.DllBase;
    ev.size   = (reason == kLdrDllLoadedReason) ? data->Loaded.SizeOfImage : data->Unloaded.SizeOfImage;
    ev.fullPath.assign(name->Buffer, name->Length / sizeof(wchar_t));

    EnterCriticalSection(&st.queueCs);
    if (st.events.size() < 256) {
        st.events.push_back(std::move(ev));
    }
    LeaveCriticalSection(&st.queueCs);
}

void drainDllNotifications() {
    auto& st = diagState();
    if (!st.queueCsInit) {
        return;
    }
    std::vector<DllNotifyEvent> batch;
    EnterCriticalSection(&st.queueCs);
    batch.swap(st.events);
    LeaveCriticalSection(&st.queueCs);

    std::filesystem::path gtaDir;
    {
        std::lock_guard lk(st.gtaMutex);
        gtaDir = st.gtaDir;
    }

    for (const auto& ev : batch) {
        if (isProcessShuttingDown()) {
            break;
        }
        const std::filesystem::path p(ev.fullPath);
        const std::string lowerName = toLowerAscii(wideToUtf8(p.filename().wstring()));
        if (isUnderSystemRoot(p) && !isProxyDllName(lowerName)) {
            continue;
        }
        const std::string rel = formatPrivacyPath(p, gtaDir);
        SVPLUS_LOG_INF_CAT("mods", "Dynamic DLL %s: %s (base=0x%p, size=%u)",
                           ev.loaded ? "loaded" : "unloaded",
                           rel.c_str(), ev.base, static_cast<unsigned>(ev.size));
    }
}

std::string buildLoadedModulesSection(
    const std::filesystem::path& gtaDir,
    const std::filesystem::path& selfModulePath,
    bool                         hasEnbIni,
    bool                         hasReShadeIni) {
    std::ostringstream oss;
    HANDLE hProc = GetCurrentProcess();
    std::vector<HMODULE> mods(256);
    DWORD needed = 0;
    if (!EnumProcessModules(hProc, mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed)) {
        oss << "  EnumProcessModules failed (err=" << GetLastError() << ")\n";
        return oss.str();
    }
    const std::size_t count = (std::min<std::size_t>)(needed / sizeof(HMODULE), mods.size());
    std::size_t omittedSystem = 0;
    std::vector<std::string> nonSysLines;

    for (std::size_t i = 0; i < count; ++i) {
        if (isProcessShuttingDown()) {
            break;
        }
        wchar_t modPath[MAX_PATH]{};
        if (GetModuleFileNameW(mods[i], modPath, MAX_PATH) == 0) {
            continue;
        }
        const std::filesystem::path p(modPath);
        const std::string lowerFile = toLowerAscii(wideToUtf8(p.filename().wstring()));
        if (isUnderSystemRoot(p) && !isProxyDllName(lowerFile)) {
            ++omittedSystem;
            continue;
        }

        const auto meta = readFileVersionMeta(p);
        std::uint64_t fsize = 0;
        std::error_code ec;
        const bool inGta = startsWithNoCase(wideToUtf8(p.wstring()), wideToUtf8(gtaDir.wstring()));
        std::string sha;
        if (inGta) {
            sha = computeFileSha256(p, &fsize);
        } else {
            fsize = std::filesystem::file_size(p, ec);
        }

        std::ostringstream line;
        line << "  - " << formatPrivacyPath(p, gtaDir)
             << " (size=" << fsize;
        if (!meta.fileVersion.empty()) {
            line << ", ver=" << meta.fileVersion;
        }
        if (!meta.productName.empty()) {
            line << ", prod=\"" << meta.productName << "\"";
        }
        if (!sha.empty()) {
            line << ", sha256=" << sha;
        }
        line << ")";
        if (lowerFile == "gta_sa.exe") {
            if (sha == kCleanGtaSa10UsSha256) {
                line << " [GTA SA 1.0 US (no-CD)]";
            } else {
                line << " [ATTENTION: non-standard gta_sa.exe build]";
            }
        } else if (!selfModulePath.empty() && isSamePathNoCase(p, selfModulePath)) {
            line << " [SampVoice Plus v" << svplus::version::kString << " (this module)]";
        } else if (inGta && isAsiLoaderDll(p, lowerFile, sha, meta)) {
            line << " [ASI loader]";
        } else if (inGta && lowerFile == "dinput8.dll") {
            line << (sha == kCleanDinput8Sha256 ? " [GTA SA 1.0 US clean]" : " [ATTENTION: modified dinput8.dll]");
        } else if (inGta && lowerFile == "vorbisfile.dll") {
            line << (sha == kCleanVorbisFileSha256 ? " [GTA SA 1.0 US clean]" : " [ATTENTION: modified vorbisFile.dll]");
        } else if (inGta && (lowerFile == "d3d9.dll" || lowerFile == "opengl32.dll" || lowerFile == "ddraw.dll")) {
            if (containsAsciiNoCase(meta.productName, "ENB") || containsAsciiNoCase(meta.fileDescription, "ENB") || hasEnbIni) {
                line << " [ENB Series - D3D9 shader proxy]";
            } else if (containsAsciiNoCase(meta.productName, "ReShade") || containsAsciiNoCase(meta.fileDescription, "ReShade") || hasReShadeIni) {
                line << " [ReShade - post-process D3D9 proxy]";
            } else {
                line << " [ATTENTION: proxy DLL in GTA root]";
            }
        } else if (inGta && isProxyDllName(lowerFile) && lowerFile != "bass.dll") {
            line << " [ATTENTION: proxy DLL in GTA root]";
        }
        nonSysLines.push_back(line.str());
    }

    oss << "Loaded modules (" << count << " total; SystemRoot DLLs omitted=" << omittedSystem << "):\n";
    for (const auto& l : nonSysLines) {
        oss << l << '\n';
    }
    return oss.str();
}

} // namespace

std::string formatPrivacyPath(
    const std::filesystem::path& fullPath,
    const std::filesystem::path& gtaDir) {
    const std::string full = wideToUtf8(fullPath.wstring());
    if (full.empty()) {
        return "<unknown>";
    }
    const std::string gta = wideToUtf8(gtaDir.wstring());
    if (!gta.empty() && startsWithNoCase(full, gta)) {
        std::string rest = full.substr(gta.size());
        if (!rest.empty() && (rest.front() == '\\' || rest.front() == '/')) {
            return "<GTA>" + rest;
        }
        return "<GTA>" + rest;
    }
    const std::string prof = userProfileUtf8();
    if (!prof.empty() && startsWithNoCase(full, prof)) {
        return "%USERPROFILE%" + full.substr(prof.size());
    }
    return full;
}

std::string describeCodeAddressAndDetour(const void* addr) {
    if (!addr) {
        return "null";
    }
    std::uint8_t bytes[12]{};
    const void* detourTarget = nullptr;
    const std::string owner = formatModuleAndOffset(addr);
    if (!readProcessBytesSafe(addr, bytes, sizeof(bytes), &detourTarget)) {
        return owner + " [unreadable]";
    }
    std::ostringstream oss;
    oss << owner << " [" << formatBytesHex(bytes, 6) << "]";
    if (detourTarget) {
        oss << " -> DETOUR " << formatModuleAndOffset(detourTarget);
    }
    return oss.str();
}

std::string scanGtaDirectoryReport(
    const std::filesystem::path& gtaDir,
    double*                      outElapsedMs,
    const std::filesystem::path& selfModulePath) {
    const auto t0 = std::chrono::steady_clock::now();
    std::ostringstream oss;
    std::error_code ec;

    if (gtaDir.empty() || !std::filesystem::exists(gtaDir, ec)) {
        if (outElapsedMs) *outElapsedMs = 0.0;
        return "GTA directory unavailable for disk mod scan.\n";
    }

    int attentionCount = 0;
    int asiLoadersFound = 0;
    bool hasEnbIni = false;
    bool hasReShadeIni = false;
    std::vector<std::string> codeMods;
    std::vector<std::string> moonScripts;
    std::size_t totalMoonScripts = 0;
    std::vector<std::string> proxyFindings;
    std::vector<std::string> shaderInis;

    // Pre-scan root shader/post-process INIs so d3d9/graphics proxies can be identified cleanly
    for (const auto& entry : std::filesystem::directory_iterator(gtaDir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        const std::string extLower = toLowerAscii(wideToUtf8(entry.path().extension().wstring()));
        const std::string nameLower = toLowerAscii(wideToUtf8(entry.path().filename().wstring()));
        if (extLower == ".ini") {
            if (nameLower == "enbseries.ini" || nameLower == "enblocal.ini") {
                hasEnbIni = true;
                shaderInis.push_back("  - [shader] " + formatPrivacyPath(entry.path(), gtaDir));
            } else if (nameLower.rfind("reshade", 0) == 0) {
                hasReShadeIni = true;
                shaderInis.push_back("  - [shader] " + formatPrivacyPath(entry.path(), gtaDir));
            }
        }
    }

    auto inspectBinaryFile = [&](const std::filesystem::path& p, std::string_view tag, bool computeHash) {
        if (isProcessShuttingDown()) return;
        std::error_code fe;
        const std::uint64_t fsize = std::filesystem::file_size(p, fe);
        const auto meta = readFileVersionMeta(p);
        const std::string fname = wideToUtf8(p.filename().wstring());
        const std::string lower = toLowerAscii(fname);
        const bool isSelf = !selfModulePath.empty() && isSamePathNoCase(p, selfModulePath);
        const std::string extLower = toLowerAscii(wideToUtf8(p.extension().wstring()));

        std::string sha;
        if (computeHash || isSelf) {
            sha = computeFileSha256(p, nullptr);
        }

        std::ostringstream line;
        line << "  - [" << tag << "] " << formatPrivacyPath(p, gtaDir) << " (" << fsize << " B";
        if (!meta.fileVersion.empty()) line << ", ver=" << meta.fileVersion;
        if (!meta.productName.empty()) line << ", \"" << meta.productName << "\"";
        if (!sha.empty()) line << ", sha256=" << sha;
        line << ")";

        bool identified = false;
        if (isSelf) {
            line << " -> SampVoice Plus v" << svplus::version::kString << " (this module)";
            identified = true;
        } else if (extLower == ".asi" && isOtherVoiceClientAsi(p, lower, selfModulePath)) {
            line << " [ATTENTION] second voice client";
            ++attentionCount;
            identified = true;
        } else if (lower == "gta_sa.exe") {
            if (sha == kCleanGtaSa10UsSha256) {
                line << " -> GTA SA 1.0 US (no-CD)";
            } else {
                line << " [ATTENTION: non-standard gta_sa.exe]";
                ++attentionCount;
            }
            identified = true;
        } else if (lower == "dinput8.dll") {
            if (sha == kCleanDinput8Sha256) {
                line << " -> clean GTA 1.0 US";
            } else if (isAsiLoaderDll(p, lower, sha, meta)) {
                ++asiLoadersFound;
                line << " -> ASI loader";
            } else {
                line << " [ATTENTION: modified dinput8.dll]";
                ++attentionCount;
            }
            identified = true;
        } else if (lower == "vorbisfile.dll") {
            if (sha == kCleanVorbisFileSha256) {
                line << " -> clean GTA 1.0 US";
            } else if (isAsiLoaderDll(p, lower, sha, meta)) {
                ++asiLoadersFound;
                line << " -> ASI loader";
            } else {
                line << " [ATTENTION: modified vorbisFile.dll]";
                ++attentionCount;
            }
            identified = true;
        } else if (tag == "proxy") {
            if (isAsiLoaderDll(p, lower, sha, meta)) {
                ++asiLoadersFound;
                line << " -> ASI loader";
            } else if ((lower == "d3d9.dll" || lower == "opengl32.dll" || lower == "ddraw.dll") &&
                       (containsAsciiNoCase(meta.productName, "ENB") ||
                        containsAsciiNoCase(meta.fileDescription, "ENB") ||
                        hasEnbIni)) {
                line << " -> ENB Series [ATTENTION: D3D9 shader proxy; hooks Present/Reset]";
                ++attentionCount;
            } else if ((lower == "d3d9.dll" || lower == "opengl32.dll" || lower == "ddraw.dll") &&
                       (containsAsciiNoCase(meta.productName, "ReShade") ||
                        containsAsciiNoCase(meta.fileDescription, "ReShade") ||
                        hasReShadeIni)) {
                line << " -> ReShade [ATTENTION: post-process D3D9 proxy; hooks Present/Reset]";
                ++attentionCount;
            } else {
                line << " [ATTENTION: proxy DLL in GTA root]";
                ++attentionCount;
            }
            identified = true;
        } else if (const char* probable = identifyProbableModName(lower)) {
            line << " -> " << probable;
            identified = true;
        }

        if (!identified && !meta.hasVersionInfo) {
            line << " [unversioned]";
        }

        if (tag == "proxy" || tag == "core") {
            proxyFindings.push_back(line.str());
        } else {
            codeMods.push_back(line.str());
        }
    };

    // 1. GTA root executables & proxy DLLs
    static constexpr const wchar_t* kRootBins[] = {
        L"gta_sa.exe", L"dinput8.dll", L"vorbisFile.dll",
        L"d3d9.dll", L"dsound.dll", L"winmm.dll", L"opengl32.dll", L"ddraw.dll"
    };
    for (const wchar_t* binName : kRootBins) {
        const auto p = gtaDir / binName;
        if (std::filesystem::exists(p, ec)) {
            const bool isCore = (_wcsicmp(binName, L"gta_sa.exe") == 0 ||
                                 _wcsicmp(binName, L"dinput8.dll") == 0 ||
                                 _wcsicmp(binName, L"vorbisFile.dll") == 0);
            inspectBinaryFile(p, isCore ? "core" : "proxy", true);
        }
    }
    if (asiLoadersFound >= 2) {
        ++attentionCount;
        proxyFindings.push_back("  [ATTENTION] more than one ASI loader detected (" +
                                std::to_string(asiLoadersFound) + "); plugins may load twice");
    }

    // 2. Root .asi modules
    for (const auto& entry : std::filesystem::directory_iterator(gtaDir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        const std::string extLower = toLowerAscii(wideToUtf8(entry.path().extension().wstring()));
        const std::string nameLower = toLowerAscii(wideToUtf8(entry.path().filename().wstring()));
        if (extLower == ".asi") {
            inspectBinaryFile(entry.path(), "asi", nameLower == "sampvoice.asi");
        }
    }

    auto scanSubdirByExts = [&](const std::filesystem::path& sub,
                                std::string_view tag,
                                bool recursive,
                                const std::vector<std::string_view>& exts) {
        std::error_code de;
        if (!std::filesystem::exists(sub, de)) return;
        auto handleEntry = [&](const std::filesystem::directory_entry& e) {
            if (isProcessShuttingDown() || !e.is_regular_file(de)) return;
            const std::string ext = toLowerAscii(wideToUtf8(e.path().extension().wstring()));
            if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
                inspectBinaryFile(e.path(), tag, false);
            }
        };
        if (recursive) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(sub, de)) {
                handleEntry(e);
            }
        } else {
            for (const auto& e : std::filesystem::directory_iterator(sub, de)) {
                handleEntry(e);
            }
        }
    };

    // 3. scripts\*.asi, cleo\*.cleo, SAMPFUNCS\*.sf, modloader\**\*.{asi,dll} (explicitly skipping .cs, data/, .img)
    scanSubdirByExts(gtaDir / L"scripts",   "scripts",   false, {".asi"});
    scanSubdirByExts(gtaDir / L"cleo",      "cleo",      false, {".cleo"});
    scanSubdirByExts(gtaDir / L"SAMPFUNCS", "sampfuncs", false, {".sf"});
    scanSubdirByExts(gtaDir / L"modloader", "modloader", true,  {".asi", ".dll"});

    // 4. moonloader\*.lua / .luac (cap logged names at 50, track total)
    const auto moonDir = gtaDir / L"moonloader";
    if (std::filesystem::exists(moonDir, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(moonDir, ec)) {
            if (isProcessShuttingDown() || !e.is_regular_file(ec)) continue;
            const std::string ext = toLowerAscii(wideToUtf8(e.path().extension().wstring()));
            if (ext == ".lua" || ext == ".luac") {
                ++totalMoonScripts;
                if (moonScripts.size() < 50) {
                    moonScripts.push_back(wideToUtf8(e.path().filename().wstring()));
                }
            }
        }
    }

    oss << "GTA Root Binaries & Proxies (" << proxyFindings.size() << "):\n";
    for (const auto& l : proxyFindings) oss << l << '\n';

    oss << "Detected Code Mods (" << codeMods.size() << "):\n";
    if (codeMods.empty()) {
        oss << "  (none outside root)\n";
    } else {
        for (const auto& l : codeMods) oss << l << '\n';
    }

    if (!shaderInis.empty()) {
        oss << "Shader / Post-Process Configs (" << shaderInis.size() << ") [ATTENTION]:\n";
        ++attentionCount;
        for (const auto& l : shaderInis) oss << l << '\n';
    }

    if (totalMoonScripts > 0) {
        oss << "MoonLoader Scripts (" << totalMoonScripts << " total";
        if (totalMoonScripts > 50) oss << ", showing first 50";
        oss << "): ";
        for (std::size_t i = 0; i < moonScripts.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << moonScripts[i];
        }
        oss << '\n';
    }

    // Static GTA SA hook points (CalcScreenCoors & CPed::GetBonePosition)
    oss << "GTA Hook Targets:\n"
        << "  - CalcScreenCoors (0x71DA00): " << describeCodeAddressAndDetour(reinterpret_cast<const void*>(0x71DA00u)) << '\n'
        << "  - CPed::GetBonePosition (0x5E4280): " << describeCodeAddressAndDetour(reinterpret_cast<const void*>(0x5E4280u)) << '\n';

    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (outElapsedMs) {
        *outElapsedMs = ms;
    }
    oss << "Attention flags: " << attentionCount << " | Disk+Static scan time: "
        << std::to_string(static_cast<int>(ms + 0.5)) << " ms";
    return oss.str();
}

void startBackgroundDiagnostics(
    const std::filesystem::path& gtaDir,
    const SampModuleDetection&   sampDetection,
    HMODULE                      selfModule) {
    auto& st = diagState();
    if (st.running.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::filesystem::path selfModPath;
    if (selfModule) {
        wchar_t buf[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(selfModule, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            selfModPath.assign(buf, buf + len);
        }
    }
    {
        std::lock_guard lk(st.gtaMutex);
        st.gtaDir = gtaDir;
        st.selfPath = wideToUtf8(selfModPath.wstring());
    }
    if (!st.queueCsInit) {
        InitializeCriticalSection(&st.queueCs);
        st.queueCsInit = true;
    }

    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto regFn = reinterpret_cast<LdrRegisterDllNotificationFn>(
            GetProcAddress(ntdll, "LdrRegisterDllNotification"));
        if (regFn && !st.ldrCookie) {
            regFn(0, &onLdrDllNotification, nullptr, &st.ldrCookie);
        }
    }

    const SampModuleDetection sampCopy = sampDetection;
    st.worker = new std::thread([sampCopy, selfModPath]() {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        const auto tStart = std::chrono::steady_clock::now();

        std::filesystem::path localGtaDir;
        {
            std::lock_guard lk(diagState().gtaMutex);
            localGtaDir = diagState().gtaDir;
        }

        std::error_code ec;
        bool hasEnb = std::filesystem::exists(localGtaDir / L"enbseries.ini", ec) ||
                      std::filesystem::exists(localGtaDir / L"enblocal.ini", ec);
        bool hasReShade = false;
        for (const auto& entry : std::filesystem::directory_iterator(localGtaDir, ec)) {
            if (ec || !entry.is_regular_file(ec)) continue;
            const std::string extLower = toLowerAscii(wideToUtf8(entry.path().extension().wstring()));
            const std::string nameLower = toLowerAscii(wideToUtf8(entry.path().filename().wstring()));
            if (extLower == ".ini" && nameLower.rfind("reshade", 0) == 0) {
                hasReShade = true;
                break;
            }
        }

        std::ostringstream report;
        report << "SA-MP module: " << formatPrivacyPath(sampCopy.modulePath, localGtaDir)
               << " (ver=" << samp::getSampLayout(sampCopy.version).name
               << ", stamp=0x" << std::hex << std::uppercase << sampCopy.timeDateStamp
               << ", EP=0x" << sampCopy.entryPointRva << std::dec << ")\n";
        report << buildLoadedModulesSection(localGtaDir, selfModPath, hasEnb, hasReShade);
        double diskMs = 0.0;
        report << scanGtaDirectoryReport(localGtaDir, &diskMs, selfModPath);

        const auto tEnd = std::chrono::steady_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        char timeBuf[64];
        std::snprintf(timeBuf, sizeof(timeBuf), "\nBackground diagnostics finished in %.1f ms", totalMs);
        report << timeBuf;

        svplus::log::Logger::LogRawBlock(svplus::log::Level::Info, "mods", report.str());

        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);

        // Drain dynamic DLL load/unload notifications lightly while active
        while (diagState().running.load(std::memory_order_acquire) && !isProcessShuttingDown()) {
            drainDllNotifications();
            Sleep(250);
        }
    });
}

void reportD3D9HookInspection(
    const char*         hookName,
    const void*         targetAddr,
    const std::uint8_t* bytesBefore,
    std::size_t         bytesLen) {
    if (!targetAddr || !hookName) return;
    std::string beforeDesc = "unreadable";
    if (bytesBefore && bytesLen >= 6) {
        std::ostringstream b;
        b << "[" << formatBytesHex(bytesBefore, 6) << "]";
        if (bytesBefore[0] == 0xE9 || bytesBefore[0] == 0xEB ||
            (bytesBefore[0] == 0xFF && bytesBefore[1] == 0x25) ||
            (bytesBefore[0] == 0x68 && bytesBefore[5] == 0xC3)) {
            b << " [ATTENTION: already hooked before SampVoice!]";
        }
        beforeDesc = b.str();
    }
    const std::string afterDesc = describeCodeAddressAndDetour(targetAddr);
    SVPLUS_LOG_INF_CAT("mods", "D3D9::%s before=%s | after=%s",
                       hookName, beforeDesc.c_str(), afterDesc.c_str());
}

void reportWndProcAttach(HWND hwnd, WNDPROC origProc, WNDPROC ourProc) {
    diagState().lastObservedWndProc.store(ourProc, std::memory_order_release);
    SVPLUS_LOG_INF_CAT("mods", "WndProc attach hwnd=0x%p: prev=%s | active=%s",
                       static_cast<void*>(hwnd),
                       formatModuleAndOffset(reinterpret_cast<const void*>(origProc)).c_str(),
                       formatModuleAndOffset(reinterpret_cast<const void*>(ourProc)).c_str());
}

void checkWndProcChange(HWND hwnd, WNDPROC ourProc) {
    if (!hwnd) return;
    const auto current = reinterpret_cast<WNDPROC>(GetWindowLongA(hwnd, GWL_WNDPROC));
    WNDPROC prev = diagState().lastObservedWndProc.load(std::memory_order_acquire);
    if (current && current != prev) {
        if (diagState().lastObservedWndProc.compare_exchange_strong(prev, current, std::memory_order_acq_rel)) {
            SVPLUS_LOG_INF_CAT("mods", "WndProc changed after attach (hwnd=0x%p): %s -> %s%s",
                               static_cast<void*>(hwnd),
                               formatModuleAndOffset(reinterpret_cast<const void*>(prev)).c_str(),
                               formatModuleAndOffset(reinterpret_cast<const void*>(current)).c_str(),
                               (current != ourProc) ? " [ATTENTION: third-party WndProc wrap]" : "");
        }
    }
}

void reportBassHookInspection(HMODULE bassMod) {
    if (!bassMod) return;
    static constexpr const char* kBassExports[] = {
        "BASS_Init", "BASS_StreamCreate", "BASS_ChannelPlay",
        "BASS_RecordInit", "BASS_RecordStart", "BASS_ChannelSetAttribute"
    };
    std::ostringstream oss;
    oss << "BASS export hook check:\n";
    for (const char* name : kBassExports) {
        const void* fn = reinterpret_cast<const void*>(GetProcAddress(bassMod, name));
        oss << "  - " << name << ": " << describeCodeAddressAndDetour(fn) << '\n';
    }
    svplus::log::Logger::LogRawBlock(svplus::log::Level::Info, "mods", oss.str());
}

namespace {

bool tryReadPtrSafe(const void* src, const void*& outPtr) noexcept {
    outPtr = nullptr;
    if (!src) return false;
    __try {
        outPtr = *static_cast<const void* const*>(src);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool tryReadVtableSlotSafe(const void* const* vtable, std::size_t idx, const void*& outFn) noexcept {
    outFn = nullptr;
    if (!vtable) return false;
    __try {
        outFn = vtable[idx];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void reportRakClientInspection(const void* rakClientPtr) {
    if (!rakClientPtr) return;
    const void* vtableRaw = nullptr;
    if (!tryReadPtrSafe(rakClientPtr, vtableRaw) || !vtableRaw) {
        return;
    }
    const auto* const* vtable = static_cast<const void* const*>(vtableRaw);

    struct SlotDesc { std::size_t idx; const char* name; };
    static constexpr SlotDesc kKeySlots[] = {
        {1, "Connect"}, {2, "Disconnect"}, {6, "Send"}, {8, "Receive"}, {25, "RPC"}
    };
    std::ostringstream oss;
    oss << "RakClient vtable inspection (ptr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(rakClientPtr) << std::dec << "):\n";
    for (const auto& s : kKeySlots) {
        const void* fn = nullptr;
        static_cast<void>(tryReadVtableSlotSafe(vtable, s.idx, fn));
        oss << "  - [" << s.idx << "] " << s.name << ": " << describeCodeAddressAndDetour(fn) << '\n';
    }
    svplus::log::Logger::LogRawBlock(svplus::log::Level::Info, "mods", oss.str());
}

void shutdownDiagnostics(bool processTerminating) noexcept {
    auto& st = diagState();
    st.running.store(false, std::memory_order_release);
    if (processTerminating) {
        return;
    }
    if (st.ldrCookie) {
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto unregFn = reinterpret_cast<LdrUnregisterDllNotificationFn>(
                GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
            if (unregFn) {
                unregFn(st.ldrCookie);
            }
        }
        st.ldrCookie = nullptr;
    }
    if (st.worker) {
        if (st.worker->joinable()) {
            st.worker->detach();
        }
        delete st.worker;
        st.worker = nullptr;
    }
}

} // namespace svplus::client::diag
