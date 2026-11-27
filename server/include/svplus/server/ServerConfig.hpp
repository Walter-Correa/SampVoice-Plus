/*
 * SampVoice Plus - Unified Server Configuration Parser & Resolver
 *
 * Reads server.cfg (SA-MP) once and resolves open.mp / SA-MP keys, aliases,
 * host bind fallbacks, conflict warnings, and validation uniformly.
 */

#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "svplus/common/Logger.hpp"
#include "svplus/server/ServerCore.hpp"

namespace svplus::server {

struct ConfigEntry {
    std::string rawValue;
    std::string source; // e.g. "config.json", "server.cfg"
};

class ServerCfgMap {
public:
    bool LoadFromFile(const std::string& path = "server.cfg") {
        entries_.clear();
        loaded_ = true;

        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const std::size_t firstNonWs = line.find_first_not_of(" \t");
            if (firstNonWs == std::string::npos) {
                continue;
            }
            if (line[firstNonWs] == '#' ||
                (line[firstNonWs] == '/' && firstNonWs + 1 < line.size() && line[firstNonWs + 1] == '/')) {
                continue;
            }

            const std::size_t keyEnd = line.find_first_of(" \t", firstNonWs);
            if (keyEnd == std::string::npos) {
                continue;
            }

            const std::string key = line.substr(firstNonWs, keyEnd - firstNonWs);
            const std::size_t valStart = line.find_first_not_of(" \t", keyEnd);
            if (valStart == std::string::npos) {
                continue;
            }

            std::string value = line.substr(valStart);
            // Strip inline # or // comment if preceded by whitespace (or at start of value)
            for (std::size_t i = 0; i < value.size(); ++i) {
                const bool isCommentStart =
                    value[i] == '#' ||
                    (value[i] == '/' && i + 1 < value.size() && value[i + 1] == '/');
                if (isCommentStart && (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t')) {
                    value.erase(i);
                    break;
                }
            }

            const std::size_t valEnd = value.find_last_not_of(" \t");
            if (valEnd == std::string::npos) {
                continue;
            }
            value.resize(valEnd + 1);

            entries_[key] = std::move(value);
        }

        return true;
    }

    bool IsLoaded() const noexcept { return loaded_; }

    bool TryGet(const std::string& key, std::string& outValue) const {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            return false;
        }
        outValue = it->second;
        return true;
    }

private:
    bool loaded_{false};
    std::unordered_map<std::string, std::string> entries_;
};

struct ConfigResolverBackend {
    // Returns true if key exists explicitly (with raw string representation in outVal and source tag in outSrc).
    virtual bool TryGetRaw(const std::string& key, std::string& outVal, std::string& outSrc) const = 0;
    virtual ~ConfigResolverBackend() = default;
};

namespace detail {

inline bool ParseStrictInt(const std::string& text, long long& outVal) noexcept {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long val = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0') {
        return false;
    }
    outVal = val;
    return true;
}

inline bool ParseStrictBool(const std::string& text, bool& outVal) noexcept {
    if (text == "true" || text == "1") {
        outVal = true;
        return true;
    }
    if (text == "false" || text == "0") {
        outVal = false;
        return true;
    }
    return false;
}

struct SelectedKey {
    bool found{false};
    std::string key;
    std::string value;
    std::string source;
};

inline SelectedKey PickFirstWithConflictWarning(
    const ConfigResolverBackend& backend,
    std::initializer_list<const char*> keys,
    bool ignoreZeroValue = false
) {
    std::vector<SelectedKey> present;
    for (const char* k : keys) {
        std::string val, src;
        if (backend.TryGetRaw(k, val, src)) {
            if (ignoreZeroValue && val == "0") {
                continue;
            }
            present.push_back(SelectedKey{true, k, val, src});
        }
    }

    if (present.empty()) {
        return {};
    }

    if (present.size() > 1) {
        for (std::size_t i = 1; i < present.size(); ++i) {
            log::Logger::Log(
                log::Level::Warn,
                "Both '%s' (%s) and '%s' (%s) are configured; using '%s=%s'",
                present[0].key.c_str(), present[0].source.c_str(),
                present[i].key.c_str(), present[i].source.c_str(),
                present[0].key.c_str(), present[0].value.c_str()
            );
        }
    }

    return present[0];
}

inline std::string FormatKeyOrigin(
    const std::string& key,
    const char* canonicalKey,
    const std::string& source
) {
    if (key == canonicalKey) {
        return source;
    }
    return key + ", " + source;
}

inline bool ResolveBoolOption(
    const ConfigResolverBackend& backend,
    std::initializer_list<const char*> keys,
    const char* canonicalKey,
    bool defaultValue,
    bool& outValue,
    std::string& outOrigin
) {
    outValue = defaultValue;
    outOrigin = "default";
    auto selected = PickFirstWithConflictWarning(backend, keys);
    if (!selected.found) {
        return true;
    }
    bool parsed = false;
    if (ParseStrictBool(selected.value, parsed)) {
        outValue = parsed;
        outOrigin = FormatKeyOrigin(selected.key, canonicalKey, selected.source);
        return true;
    }
    log::Logger::Log(
        log::Level::Warn,
        "Invalid boolean '%s' for %s (%s); using default %s",
        selected.value.c_str(),
        selected.key.c_str(),
        selected.source.c_str(),
        defaultValue ? "true" : "false"
    );
    outOrigin = "default, invalid " + selected.key;
    return false;
}

} // namespace detail

inline bool ResolveServerStartOptions(
    const ConfigResolverBackend& backend,
    const std::string& hostBindFallbackKey,
    ServerStartOptions& outOptions
) {
    outOptions = ServerStartOptions{};

    // 1. Voice UDP port
    // Allow fallback from defaulted sampvoice.port=0 only if user explicitly configured an alias,
    // while still picking up an explicit sampvoice.port if set (including negative/out-of-range).
    std::string rawPrimaryPort, srcPrimaryPort;
    const bool hasPrimaryPort = backend.TryGetRaw("sampvoice.port", rawPrimaryPort, srcPrimaryPort);

    detail::SelectedKey selectedPort;
    if (hasPrimaryPort && rawPrimaryPort != "0") {
        selectedPort = {true, "sampvoice.port", rawPrimaryPort, srcPrimaryPort};
        // Warn if any alias is also set
        for (const char* alias : {"sampvoice_port", "sv_voice_port"}) {
            std::string av, as;
            if (backend.TryGetRaw(alias, av, as) && av != "0") {
                log::Logger::Log(
                    log::Level::Warn,
                    "Both 'sampvoice.port' (%s) and '%s' (%s) are configured; using 'sampvoice.port=%s'",
                    srcPrimaryPort.c_str(), alias, as.c_str(), rawPrimaryPort.c_str()
                );
            }
        }
    } else {
        selectedPort = detail::PickFirstWithConflictWarning(
            backend, {"sampvoice_port", "sv_voice_port"}, false
        );
        if (!selectedPort.found && hasPrimaryPort) {
            selectedPort = {true, "sampvoice.port", rawPrimaryPort, srcPrimaryPort};
        }
    }

    if (selectedPort.found) {
        long long portVal = 0;
        if (!detail::ParseStrictInt(selectedPort.value, portVal) || portVal < 0 || portVal > 65535) {
            log::Logger::Log(
                log::Level::Error,
                "Invalid %s value '%s' from %s (expected 0 or 1..65535); refusing to start on random port",
                selectedPort.key.c_str(), selectedPort.value.c_str(), selectedPort.source.c_str()
            );
            return false;
        }
        outOptions.portOrigin = detail::FormatKeyOrigin(selectedPort.key, "sampvoice.port", selectedPort.source);
        if (portVal == 0) {
            outOptions.udpPort = 0;
            outOptions.explicitPort = false;
        } else {
            outOptions.udpPort = static_cast<uint16_t>(portVal);
            outOptions.explicitPort = true;
        }
    }

    // 2. Voice bind IP
    auto selectedBind = detail::PickFirstWithConflictWarning(
        backend, {"sampvoice.bind", "sampvoice_bind"}
    );
    if (selectedBind.found && !selectedBind.value.empty()) {
        outOptions.bindIp = selectedBind.value;
        outOptions.bindOrigin = detail::FormatKeyOrigin(selectedBind.key, "sampvoice.bind", selectedBind.source);
    } else if (!hostBindFallbackKey.empty()) {
        std::string hostBind, hostSrc;
        if (backend.TryGetRaw(hostBindFallbackKey, hostBind, hostSrc) &&
            !hostBind.empty() && hostBind != "0.0.0.0") {
            outOptions.bindIp = hostBind;
            outOptions.bindOrigin = "inherited from " + hostBindFallbackKey + ", " + hostSrc;
        }
    }

    // 3. Worker threads
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    auto selectedThreads = detail::PickFirstWithConflictWarning(
        backend, {"sampvoice.threads", "sampvoice_threads"}
    );
    if (selectedThreads.found) {
        long long tVal = 0;
        if (!detail::ParseStrictInt(selectedThreads.value, tVal) || tVal <= 0) {
            log::Logger::Log(
                log::Level::Warn,
                "Invalid %s value '%s' (%s); using default 4",
                selectedThreads.key.c_str(), selectedThreads.value.c_str(), selectedThreads.source.c_str()
            );
            outOptions.workerThreadCount = 4;
            outOptions.threadsOrigin = "default, invalid " + selectedThreads.key;
        } else {
            const uint32_t clamped = static_cast<uint32_t>(std::clamp<long long>(tVal, 1, static_cast<long long>(hwThreads)));
            if (clamped != tVal) {
                log::Logger::Log(
                    log::Level::Warn,
                    "Clamped %s=%lld to %u (max hardware concurrency)",
                    selectedThreads.key.c_str(), tVal, clamped
                );
                outOptions.threadsOrigin = selectedThreads.key + ", clamped from " + std::to_string(tVal);
            } else {
                outOptions.threadsOrigin = detail::FormatKeyOrigin(
                    selectedThreads.key, "sampvoice.threads", selectedThreads.source
                );
            }
            outOptions.workerThreadCount = clamped;
        }
    } else {
        outOptions.workerThreadCount = std::clamp(4u, 1u, hwThreads);
        outOptions.threadsOrigin = "default";
    }

    // 4. Spatial grid update rate (ms)
    auto selectedRate = detail::PickFirstWithConflictWarning(
        backend, {"sampvoice.updaterate", "sampvoice.update_rate", "sampvoice_updaterate"}
    );
    if (selectedRate.found) {
        long long rVal = 0;
        if (!detail::ParseStrictInt(selectedRate.value, rVal) || rVal <= 0) {
            log::Logger::Log(
                log::Level::Warn,
                "Invalid %s value '%s' (%s); using default 30 ms",
                selectedRate.key.c_str(), selectedRate.value.c_str(), selectedRate.source.c_str()
            );
            outOptions.updateRateMs = 30;
            outOptions.rateOrigin = "default, invalid " + selectedRate.key;
        } else {
            const uint32_t clamped = static_cast<uint32_t>(std::clamp<long long>(rVal, 10, 1000));
            if (clamped != rVal) {
                log::Logger::Log(
                    log::Level::Warn,
                    "Clamped %s=%lld ms to %u ms [10..1000]",
                    selectedRate.key.c_str(), rVal, clamped
                );
                outOptions.rateOrigin = selectedRate.key + ", clamped from " + std::to_string(rVal);
            } else {
                outOptions.rateOrigin = detail::FormatKeyOrigin(
                    selectedRate.key, "sampvoice.updaterate", selectedRate.source
                );
            }
            outOptions.updateRateMs = clamped;
        }
    }

    // 5. Stream color byte order (default false = SA-MP 0xRRGGBBAA; true = legacy 0xAARRGGBB)
    detail::ResolveBoolOption(
        backend,
        {"sampvoice.legacy_color_format", "legacy_color_format"},
        "sampvoice.legacy_color_format",
        false,
        outOptions.legacyColorFormat,
        outOptions.legacyColorOrigin
    );

    // 6. Voice status indicator (default true)
    detail::ResolveBoolOption(
        backend,
        {"sampvoice.voice_status"},
        "sampvoice.voice_status",
        true,
        outOptions.voiceStatus,
        outOptions.voiceStatusOrigin
    );

    // 7. Nearby list filter in F11 client UI (default true)
    detail::ResolveBoolOption(
        backend,
        {"sampvoice.nearby_list"},
        "sampvoice.nearby_list",
        true,
        outOptions.nearbyList,
        outOptions.nearbyListOrigin
    );

    // 8. Allow client Toggle talk mode (default true)
    detail::ResolveBoolOption(
        backend,
        {"sampvoice.allow_toggle_mode"},
        "sampvoice.allow_toggle_mode",
        true,
        outOptions.allowToggleMode,
        outOptions.allowToggleModeOrigin
    );

    // 9. Positional voice occlusion behind walls (default true)
    detail::ResolveBoolOption(
        backend,
        {"sampvoice.voice_occlusion"},
        "sampvoice.voice_occlusion",
        true,
        outOptions.voiceOcclusion,
        outOptions.voiceOcclusionOrigin
    );

    // 10. Priority channel ducking percentage (0..100, default 50)
    auto selectedDuck = detail::PickFirstWithConflictWarning(
        backend, {"sampvoice.priority_duck"}
    );
    if (selectedDuck.found) {
        long long dVal = 0;
        if (!detail::ParseStrictInt(selectedDuck.value, dVal)) {
            log::Logger::Log(
                log::Level::Warn,
                "Invalid %s value '%s' (%s); using default 50",
                selectedDuck.key.c_str(), selectedDuck.value.c_str(), selectedDuck.source.c_str()
            );
            outOptions.priorityDuckPct = 50;
            outOptions.priorityDuckOrigin = "default, invalid " + selectedDuck.key;
        } else {
            const uint32_t clamped = static_cast<uint32_t>(std::clamp<long long>(dVal, 0, 100));
            if (clamped != dVal) {
                log::Logger::Log(
                    log::Level::Warn,
                    "Clamped %s=%lld to %u [0..100]",
                    selectedDuck.key.c_str(), dVal, clamped
                );
                outOptions.priorityDuckOrigin = selectedDuck.key + ", clamped from " + std::to_string(dVal);
            } else {
                outOptions.priorityDuckOrigin = detail::FormatKeyOrigin(
                    selectedDuck.key, "sampvoice.priority_duck", selectedDuck.source
                );
            }
            outOptions.priorityDuckPct = clamped;
        }
    }

    return true;
}

} // namespace svplus::server
