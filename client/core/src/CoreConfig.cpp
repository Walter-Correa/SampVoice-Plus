#include "svplus/client/CoreConfig.hpp"

#include <algorithm>
#include <cctype>

namespace svplus::client {
namespace {

bool equalsIgnoreCaseAscii(std::string_view a, std::string_view b) noexcept {
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

} // namespace

const BlacklistEntry* CoreConfig::findOriginEntry(
    std::string_view name,
    std::string_view originEndpoint) const noexcept {
    if (name.empty() || originEndpoint.empty()) {
        return nullptr;
    }
    for (const auto& entry : blacklistEntries) {
        if (equalsIgnoreCaseAscii(entry.playerName, name) &&
            equalsIgnoreCaseAscii(entry.serverEndpoint, originEndpoint)) {
            return &entry;
        }
    }
    return nullptr;
}

const BlacklistEntry* CoreConfig::findBlockingEntry(
    std::string_view name,
    std::string_view activeServerEndpoint) const noexcept {
    if (name.empty()) {
        return nullptr;
    }
    if (const BlacklistEntry* local = findOriginEntry(name, activeServerEndpoint)) {
        return local;
    }
    for (const auto& entry : blacklistEntries) {
        if (entry.scope == BlacklistScope::All && equalsIgnoreCaseAscii(entry.playerName, name)) {
            return &entry;
        }
    }
    return nullptr;
}

bool CoreConfig::isPlayerMuted(
    std::string_view name,
    std::string_view activeServerEndpoint) const noexcept {
    return findBlockingEntry(name, activeServerEndpoint) != nullptr;
}

int CoreConfig::getPlayerVolumePct(
    std::string_view name,
    std::string_view activeServerEndpoint) const noexcept {
    if (name.empty() || activeServerEndpoint.empty()) {
        return kDefaultPlayerVolume;
    }
    for (const auto& entry : playerVolumeEntries) {
        if (equalsIgnoreCaseAscii(entry.playerName, name) &&
            equalsIgnoreCaseAscii(entry.serverEndpoint, activeServerEndpoint)) {
            return std::clamp(entry.volume, 0, kMaxPlayerVolume);
        }
    }
    return kDefaultPlayerVolume;
}

} // namespace svplus::client
