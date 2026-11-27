#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace svplus::client::util {

[[nodiscard]] inline bool isValidUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7Fu) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        std::uint32_t minCp = 0;
        if ((c & 0xE0u) == 0xC0u) {
            extra = 1;
            cp    = c & 0x1Fu;
            minCp = 0x80u;
        } else if ((c & 0xF0u) == 0xE0u) {
            extra = 2;
            cp    = c & 0x0Fu;
            minCp = 0x800u;
        } else if ((c & 0xF8u) == 0xF0u) {
            extra = 3;
            cp    = c & 0x07u;
            minCp = 0x10000u;
        } else {
            return false;
        }
        if (i + extra >= n) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto cont = static_cast<unsigned char>(s[i + k]);
            if ((cont & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6u) | (cont & 0x3Fu);
        }
        if (cp < minCp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        i += 1u + extra;
    }
    return true;
}

/// Portable fallback converter for legacy non-UTF-8 byte strings: preserves ASCII/valid UTF-8
/// unchanged and maps raw 8-bit ISO-8859-1 bytes to UTF-8.
inline void ensureUtf8FallbackInto(std::string_view raw, std::string& out) {
    if (raw.empty() || isValidUtf8(raw)) {
        out.assign(raw);
        return;
    }
    out.clear();
    out.reserve(raw.size() + raw.size() / 4u);
    for (unsigned char ch : raw) {
        if (ch < 0x80u) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back(static_cast<char>(0xC0u | (ch >> 6u)));
            out.push_back(static_cast<char>(0x80u | (ch & 0x3Fu)));
        }
    }
}

/// Returns the largest byte count <= maxBytes that ends cleanly on a UTF-8 codepoint boundary.
[[nodiscard]] inline std::size_t utf8TruncateLen(std::string_view s, std::size_t maxBytes) noexcept {
    if (s.size() <= maxBytes) {
        return s.size();
    }
    std::size_t cut = maxBytes;
    // Walk back past UTF-8 continuation bytes (10xxxxxx) to the leading byte.
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) {
        --cut;
    }
    return cut;
}

} // namespace svplus::client::util
