#pragma once

#include "svplus/client/Utf8Util.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace svplus::client::util {

/// Ensures text coming from SA-MP / Pawn (UTF-8 or Windows CP_ACP ANSI) is valid UTF-8,
/// writing into `out` and reusing its buffer capacity when possible.
inline void ensureUtf8NameInto(std::string_view raw, std::string& out, UINT ansiCodePage = CP_ACP) {
    if (raw.empty() || isValidUtf8(raw)) {
        out.assign(raw);
        return;
    }
    const int wideLen = MultiByteToWideChar(
        ansiCodePage, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (wideLen <= 0) {
        out.assign(raw);
        return;
    }
    std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
    if (MultiByteToWideChar(
            ansiCodePage, 0, raw.data(), static_cast<int>(raw.size()), wide.data(), wideLen) <= 0) {
        out.assign(raw);
        return;
    }
    const int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        out.assign(raw);
        return;
    }
    out.resize(static_cast<std::size_t>(utf8Len));
    if (WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), wideLen, out.data(), utf8Len, nullptr, nullptr) <= 0) {
        out.assign(raw);
    }
}

/// Ensures text coming from SA-MP / Pawn (UTF-8 or Windows CP_ACP ANSI) is valid UTF-8.
[[nodiscard]] inline std::string ensureUtf8Name(std::string_view raw, UINT ansiCodePage = CP_ACP) {
    std::string out;
    ensureUtf8NameInto(raw, out, ansiCodePage);
    return out;
}

} // namespace svplus::client::util
