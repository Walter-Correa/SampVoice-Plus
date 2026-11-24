#include "config/ClientConfig.hpp"

#include <svplus/common/Logger.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>

namespace svplus::client::config {
namespace {

std::string trim(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string{sv.substr(start, end - start)};
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool parseBool(std::string_view value, bool fallback) noexcept {
    if (equalsIgnoreCase(value, "true") || value == "1" || equalsIgnoreCase(value, "yes")) {
        return true;
    }
    if (equalsIgnoreCase(value, "false") || value == "0" || equalsIgnoreCase(value, "no")) {
        return false;
    }
    return fallback;
}

int parseInt(std::string_view value, int fallback, int minVal, int maxVal) noexcept {
    try {
        size_t idx = 0;
        const std::string s{value};
        int parsed = std::stoi(s, &idx, 0);
        if (idx == 0) {
            return fallback;
        }
        return std::clamp(parsed, minVal, maxVal);
    } catch (...) {
        return fallback;
    }
}

std::uint32_t parseUInt32(std::string_view value, std::uint32_t fallback) noexcept {
    try {
        size_t idx = 0;
        const std::string s{value};
        unsigned long parsed = std::stoul(s, &idx, 0);
        if (idx == 0) {
            return fallback;
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return fallback;
    }
}

float parseFloat(std::string_view value, float fallback, float minVal, float maxVal) noexcept {
    try {
        size_t idx = 0;
        const std::string s{value};
        float parsed = std::stof(s, &idx);
        if (idx == 0) {
            return fallback;
        }
        return std::clamp(parsed, minVal, maxVal);
    } catch (...) {
        return fallback;
    }
}

std::string sanitizePipeField(std::string_view sv) {
    std::string out = trim(sv);
    for (char& c : out) {
        if (c == '|') {
            c = '/';
        } else if (c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return trim(out);
}

bool isValidEndpointField(std::string_view ep) noexcept {
    const std::size_t colon = ep.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= ep.size()) {
        return false;
    }
    for (std::size_t i = colon + 1; i < ep.size(); ++i) {
        if (ep[i] < '0' || ep[i] > '9') {
            return false;
        }
    }
    return true;
}

const char* scopeToString(BlacklistScope s) noexcept {
    return (s == BlacklistScope::All) ? "all" : "server";
}

} // namespace

const BlacklistEntry* ClientConfig::findOriginEntry(
    std::string_view name,
    std::string_view originEndpoint) const noexcept {
    if (name.empty() || originEndpoint.empty()) {
        return nullptr;
    }
    for (const auto& entry : blacklistEntries) {
        if (equalsIgnoreCase(entry.playerName, name) &&
            equalsIgnoreCase(entry.serverEndpoint, originEndpoint)) {
            return &entry;
        }
    }
    return nullptr;
}

const BlacklistEntry* ClientConfig::findBlockingEntry(
    std::string_view name,
    std::string_view activeServerEndpoint) const noexcept {
    if (name.empty()) {
        return nullptr;
    }
    if (const BlacklistEntry* local = findOriginEntry(name, activeServerEndpoint)) {
        return local;
    }
    for (const auto& entry : blacklistEntries) {
        if (entry.scope == BlacklistScope::All && equalsIgnoreCase(entry.playerName, name)) {
            return &entry;
        }
    }
    return nullptr;
}

bool ClientConfig::isPlayerMuted(
    std::string_view name,
    std::string_view activeServerEndpoint) const noexcept {
    return findBlockingEntry(name, activeServerEndpoint) != nullptr;
}

bool ClientConfig::mutePlayer(
    std::string_view name,
    std::string_view serverName,
    std::string_view serverEndpoint,
    BlacklistScope   scope) {
    const std::string cleanPlayer = sanitizePipeField(name);
    const std::string cleanServer = sanitizePipeField(serverName);
    const std::string cleanEp     = sanitizePipeField(serverEndpoint);
    if (cleanPlayer.empty() || cleanEp.empty()) {
        return false;
    }

    for (auto& entry : blacklistEntries) {
        if (equalsIgnoreCase(entry.playerName, cleanPlayer) &&
            equalsIgnoreCase(entry.serverEndpoint, cleanEp)) {
            bool changed = false;
            if (entry.serverName.empty() && !cleanServer.empty()) {
                entry.serverName = cleanServer;
                changed = true;
            }
            if (entry.scope != scope) {
                const BlacklistScope oldScope = entry.scope;
                entry.scope = scope;
                ++muteGeneration;
                SVPLUS_LOG_INF_CAT(
                    "blacklist",
                    "blacklist: scope %s | %s | %s %s -> %s",
                    entry.playerName.c_str(),
                    entry.serverName.c_str(),
                    entry.serverEndpoint.c_str(),
                    scopeToString(oldScope),
                    scopeToString(entry.scope));
                return true;
            }
            return changed;
        }
    }

    blacklistEntries.push_back(BlacklistEntry{
        cleanPlayer,
        cleanServer,
        cleanEp,
        scope
    });
    ++muteGeneration;
    SVPLUS_LOG_INF_CAT(
        "blacklist",
        "blacklist: + %s | %s | %s | %s",
        cleanPlayer.c_str(),
        cleanServer.c_str(),
        cleanEp.c_str(),
        scopeToString(scope));
    return true;
}

bool ClientConfig::removeBlacklistEntry(std::string_view name, std::string_view originEndpoint) {
    for (auto it = blacklistEntries.begin(); it != blacklistEntries.end(); ++it) {
        if (equalsIgnoreCase(it->playerName, name) &&
            equalsIgnoreCase(it->serverEndpoint, originEndpoint)) {
            SVPLUS_LOG_INF_CAT(
                "blacklist",
                "blacklist: - %s | %s | %s | %s",
                it->playerName.c_str(),
                it->serverName.c_str(),
                it->serverEndpoint.c_str(),
                scopeToString(it->scope));
            blacklistEntries.erase(it);
            ++muteGeneration;
            return true;
        }
    }
    return false;
}

bool ClientConfig::setBlacklistEntryScope(
    std::string_view name,
    std::string_view originEndpoint,
    BlacklistScope   scope) {
    for (auto& entry : blacklistEntries) {
        if (equalsIgnoreCase(entry.playerName, name) &&
            equalsIgnoreCase(entry.serverEndpoint, originEndpoint)) {
            if (entry.scope == scope) {
                return false;
            }
            const BlacklistScope oldScope = entry.scope;
            entry.scope = scope;
            ++muteGeneration;
            SVPLUS_LOG_INF_CAT(
                "blacklist",
                "blacklist: scope %s | %s | %s %s -> %s",
                entry.playerName.c_str(),
                entry.serverName.c_str(),
                entry.serverEndpoint.c_str(),
                scopeToString(oldScope),
                scopeToString(scope));
            return true;
        }
    }
    return false;
}

std::size_t ClientConfig::clearOriginServerBlacklist(std::string_view originEndpoint) {
    if (originEndpoint.empty()) {
        return 0;
    }
    std::size_t removed = 0;
    for (auto it = blacklistEntries.begin(); it != blacklistEntries.end();) {
        if (equalsIgnoreCase(it->serverEndpoint, originEndpoint)) {
            SVPLUS_LOG_INF_CAT(
                "blacklist",
                "blacklist: - %s | %s | %s | %s",
                it->playerName.c_str(),
                it->serverName.c_str(),
                it->serverEndpoint.c_str(),
                scopeToString(it->scope));
            it = blacklistEntries.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        ++muteGeneration;
    }
    return removed;
}

bool ClientConfig::refreshServerNameForEndpoint(
    std::string_view originEndpoint,
    std::string_view serverName) {
    if (originEndpoint.empty()) {
        return false;
    }
    const std::string cleanServer = sanitizePipeField(serverName);
    if (cleanServer.empty()) {
        return false;
    }
    bool changed = false;
    for (auto& entry : blacklistEntries) {
        if (equalsIgnoreCase(entry.serverEndpoint, originEndpoint) &&
            entry.serverName != cleanServer) {
            entry.serverName = cleanServer;
            changed = true;
        }
    }
    return changed;
}

bool ClientConfig::loadFromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    blacklistEntries.clear();
    invalidBlacklistLines.clear();
    ++muteGeneration;

    std::string line;
    std::string section;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trim(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            for (char& c : section) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            continue;
        }

        if (section == "blacklist") {
            std::vector<std::string> fields;
            std::size_t pos = 0;
            while (true) {
                const std::size_t bar = trimmed.find('|', pos);
                if (bar == std::string::npos) {
                    fields.push_back(trim(std::string_view(trimmed).substr(pos)));
                    break;
                }
                fields.push_back(trim(std::string_view(trimmed).substr(pos, bar - pos)));
                pos = bar + 1;
            }
            if (fields.size() < 3 || fields.size() > 4) {
                SVPLUS_LOG_WRN_CAT(
                    "blacklist",
                    "Keeping invalid [blacklist] line %zu (expected 3 or 4 pipe-separated fields): '%s'",
                    lineNo, trimmed.c_str());
                invalidBlacklistLines.push_back(trimmed);
                continue;
            }
            const std::string cleanPlayer = sanitizePipeField(fields[0]);
            const std::string cleanServer = sanitizePipeField(fields[1]);
            const std::string cleanEp     = sanitizePipeField(fields[2]);
            if (cleanPlayer.empty()) {
                SVPLUS_LOG_WRN_CAT(
                    "blacklist",
                    "Keeping invalid [blacklist] line %zu (empty player name): '%s'",
                    lineNo, trimmed.c_str());
                invalidBlacklistLines.push_back(trimmed);
                continue;
            }
            if (!isValidEndpointField(cleanEp)) {
                SVPLUS_LOG_WRN_CAT(
                    "blacklist",
                    "Keeping invalid [blacklist] line %zu (invalid IP:port '%s'): '%s'",
                    lineNo, cleanEp.c_str(), trimmed.c_str());
                invalidBlacklistLines.push_back(trimmed);
                continue;
            }
            BlacklistScope parsedScope = BlacklistScope::Server;
            if (fields.size() == 4) {
                if (equalsIgnoreCase(fields[3], "all")) {
                    parsedScope = BlacklistScope::All;
                } else if (equalsIgnoreCase(fields[3], "server")) {
                    parsedScope = BlacklistScope::Server;
                } else {
                    SVPLUS_LOG_WRN_CAT(
                        "blacklist",
                        "Keeping invalid [blacklist] line %zu (unknown scope '%s'; expected 'server' or 'all'): '%s'",
                        lineNo, fields[3].c_str(), trimmed.c_str());
                    invalidBlacklistLines.push_back(trimmed);
                    continue;
                }
            }

            bool merged = false;
            for (auto& existing : blacklistEntries) {
                if (equalsIgnoreCase(existing.playerName, cleanPlayer) &&
                    equalsIgnoreCase(existing.serverEndpoint, cleanEp)) {
                    if (parsedScope == BlacklistScope::All && existing.scope != BlacklistScope::All) {
                        existing.scope = BlacklistScope::All;
                    }
                    if (existing.serverName.empty() && !cleanServer.empty()) {
                        existing.serverName = cleanServer;
                    }
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                blacklistEntries.push_back(BlacklistEntry{cleanPlayer, cleanServer, cleanEp, parsedScope});
            }
            continue;
        }

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(trimmed).substr(0, eq));
        const std::string val = trim(std::string_view(trimmed).substr(eq + 1));

        if (key == "menu_hotkey") {
            const auto vk = parseUInt32(val, kDefaultMenuHotkey);
            menuHotkey = (vk != 0 && vk <= 0xFE) ? vk : kDefaultMenuHotkey;
        } else if (key == "language") {
            if (!val.empty()) {
                language = val;
            }
        } else if (key == "menu_x") {
            menuX = parseFloat(val, menuX, -1.0f, 1.0f);
        } else if (key == "menu_y") {
            menuY = parseFloat(val, menuY, -1.0f, 1.0f);
        } else if (key == "menu_w") {
            menuW = parseFloat(val, menuW, -1.0f, 1.0f);
        } else if (key == "menu_h") {
            menuH = parseFloat(val, menuH, -1.0f, 1.0f);
        } else if (key == "show_speaker_list") {
            showSpeakerList = parseBool(val, showSpeakerList);
        } else if (key == "speaker_list_header") {
            speakerListHeader = parseBool(val, speakerListHeader);
        } else if (key == "speaker_list_bg") {
            speakerListBg = parseBool(val, speakerListBg);
        } else if (key == "speaker_list_bg_opacity") {
            speakerListBgOpacity = parseInt(val, speakerListBgOpacity, 0, 100);
        } else if (key == "speaker_list_max") {
            speakerListMax = parseInt(val, speakerListMax, 1, 15);
        } else if (key == "show_micro_icon") {
            showMicroIcon = parseBool(val, showMicroIcon);
        } else if (key == "micro_icon_scale") {
            microIconScale = parseFloat(val, microIconScale, 0.5f, 2.5f);
        } else if (key == "show_overhead_icons") {
            showOverheadIcons = parseBool(val, showOverheadIcons);
        } else if (key == "overhead_icon_bg_opacity") {
            overheadIconBgOpacity = parseInt(val, overheadIconBgOpacity, 0, 100);
        } else if (key == "overhead_icon_height") {
            overheadIconHeight = parseInt(val, overheadIconHeight, 0, 60);
        } else if (key == "speaker_icon_scale") {
            speakerIconScale = parseFloat(val, speakerIconScale, 0.25f, 3.0f);
        } else if (key == "speaker_list_scale") {
            speakerListScale = parseFloat(val, speakerListScale, 0.5f, 2.5f);
        } else if (key == "micro_icon_x") {
            microIconX = parseFloat(val, kDefaultMicroIconX, -1.0f, 16384.0f);
            if (microIconX < 0.0f) {
                microIconX = kDefaultMicroIconX;
            }
        } else if (key == "micro_icon_y") {
            microIconY = parseFloat(val, kDefaultMicroIconY, -1.0f, 16384.0f);
            if (microIconY < 0.0f) {
                microIconY = kDefaultMicroIconY;
            }
        } else if (key == "speaker_list_x") {
            speakerListX = parseFloat(val, kDefaultSpeakerListX, -1.0f, 16384.0f);
            if (speakerListX < 0.0f) {
                speakerListX = kDefaultSpeakerListX;
            }
        } else if (key == "speaker_list_y") {
            speakerListY = parseFloat(val, kDefaultSpeakerListY, -1.0f, 16384.0f);
            if (speakerListY < 0.0f) {
                speakerListY = kDefaultSpeakerListY;
            }
        } else if (key == "sound_enable") {
            soundEnable = parseBool(val, soundEnable);
        } else if (key == "sound_volume") {
            soundVolume = parseInt(val, soundVolume, 0, 100);
        } else if (key == "sound_balancer") {
            soundBalancer = parseBool(val, soundBalancer);
        } else if (key == "sound_filter") {
            soundFilter = parseBool(val, soundFilter);
        } else if (key == "micro_enable") {
            microEnable = parseBool(val, microEnable);
        } else if (key == "micro_volume") {
            microVolume = parseInt(val, microVolume, 0, 100);
        } else if (key == "micro_device") {
            microDevice = val;
        } else if (key == "micro_noise_suppress") {
            microNoiseSuppress = parseBool(val, microNoiseSuppress);
        } else if (key == "talk_mode") {
            if (equalsIgnoreCase(val, "toggle") || val == "1") {
                talkMode = TalkMode::Toggle;
            } else if (equalsIgnoreCase(val, "push") || equalsIgnoreCase(val, "ptt") || val == "0") {
                talkMode = TalkMode::PushToTalk;
            }
        }
    }

    return true;
}

bool ClientConfig::saveToFile(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out.imbue(std::locale::classic());

    char hexKey[16];
    std::snprintf(hexKey, sizeof(hexKey), "0x%02X", static_cast<unsigned>(menuHotkey));

    out << "; SampVoice Plus Configuration\n"
        << "[general]\n"
        << "menu_hotkey=" << hexKey << "\n"
        << "language=" << language << "\n"
        << "menu_x=" << menuX << "\n"
        << "menu_y=" << menuY << "\n"
        << "menu_w=" << menuW << "\n"
        << "menu_h=" << menuH << "\n"
        << "show_speaker_list=" << (showSpeakerList ? 1 : 0) << "\n"
        << "speaker_list_header=" << (speakerListHeader ? 1 : 0) << "\n"
        << "speaker_list_bg=" << (speakerListBg ? 1 : 0) << "\n"
        << "speaker_list_bg_opacity=" << std::clamp(speakerListBgOpacity, 0, 100) << "\n"
        << "speaker_list_max=" << std::clamp(speakerListMax, 1, 15) << "\n"
        << "speaker_list_scale=" << speakerListScale << "\n"
        << "speaker_list_x=" << speakerListX << "\n"
        << "speaker_list_y=" << speakerListY << "\n"
        << "show_micro_icon=" << (showMicroIcon ? 1 : 0) << "\n"
        << "micro_icon_scale=" << std::clamp(microIconScale, 0.5f, 2.5f) << "\n"
        << "micro_icon_x=" << microIconX << "\n"
        << "micro_icon_y=" << microIconY << "\n"
        << "show_overhead_icons=" << (showOverheadIcons ? 1 : 0) << "\n"
        << "overhead_icon_bg_opacity=" << std::clamp(overheadIconBgOpacity, 0, 100) << "\n"
        << "overhead_icon_height=" << std::clamp(overheadIconHeight, 0, 60) << "\n"
        << "speaker_icon_scale=" << speakerIconScale << "\n"
        << "\n[sound]\n"
        << "sound_enable=" << (soundEnable ? 1 : 0) << "\n"
        << "sound_volume=" << soundVolume << "\n"
        << "sound_balancer=" << (soundBalancer ? 1 : 0) << "\n"
        << "sound_filter=" << (soundFilter ? 1 : 0) << "\n"
        << "\n[microphone]\n"
        << "micro_enable=" << (microEnable ? 1 : 0) << "\n"
        << "micro_volume=" << microVolume << "\n"
        << "micro_device=" << microDevice << "\n"
        << "micro_noise_suppress=" << (microNoiseSuppress ? 1 : 0) << "\n"
        << "talk_mode=" << (talkMode == TalkMode::Toggle ? "toggle" : "push") << "\n"
        << "\n[blacklist]\n"
        << "; <player> | <server name> | <IP>:<port> | <server or all>\n";

    for (const auto& entry : blacklistEntries) {
        out << sanitizePipeField(entry.playerName) << " | "
            << sanitizePipeField(entry.serverName) << " | "
            << sanitizePipeField(entry.serverEndpoint) << " | "
            << scopeToString(entry.scope) << "\n";
    }
    for (const auto& rawInvalid : invalidBlacklistLines) {
        out << rawInvalid << "\n";
    }

    return out.good();
}

void ClientConfig::logLoadedSummary(const std::filesystem::path& path) const {
    SVPLUS_LOG_INF_CAT(
        "config",
        "Loaded config '%s': lang=%s, hotkey=0x%02X, sound=%s(%d%% bal=%d filt=%d), "
        "mic=%s(%d%% ns=%d mode=%s dev='%s'), blacklist=%zu entry(ies), hud(list=%d hdr=%d bg=%d@%d%% max=%d scale=%.2f | "
        "mic=%d scale=%.2f | overhead=%d@%d%% height=%dpx iconScale=%.2f)",
        path.filename().string().c_str(),
        language.c_str(),
        static_cast<unsigned>(menuHotkey),
        soundEnable ? "on" : "off", soundVolume, soundBalancer ? 1 : 0, soundFilter ? 1 : 0,
        microEnable ? "on" : "off", microVolume, microNoiseSuppress ? 1 : 0,
        talkMode == TalkMode::Toggle ? "toggle" : "push",
        microDevice.empty() ? "<default>" : microDevice.c_str(),
        blacklistEntries.size(),
        showSpeakerList ? 1 : 0, speakerListHeader ? 1 : 0, speakerListBg ? 1 : 0,
        speakerListBgOpacity, speakerListMax, speakerListScale,
        showMicroIcon ? 1 : 0, microIconScale,
        showOverheadIcons ? 1 : 0, overheadIconBgOpacity, overheadIconHeight, speakerIconScale
    );
}

void ClientConfig::logConfigDiff(const ClientConfig& previous) const {
    auto diffBool = [](const char* k, bool o, bool n) {
        if (o != n) SVPLUS_LOG_INF_CAT("config", "Changed %s: %d -> %d", k, o ? 1 : 0, n ? 1 : 0);
    };
    auto diffInt = [](const char* k, int o, int n) {
        if (o != n) SVPLUS_LOG_INF_CAT("config", "Changed %s: %d -> %d", k, o, n);
    };
    auto diffFloat = [](const char* k, float o, float n) {
        if (std::fabs(o - n) >= 0.0005f) SVPLUS_LOG_INF_CAT("config", "Changed %s: %.4f -> %.4f", k, o, n);
    };
    if (previous.menuHotkey != menuHotkey) {
        SVPLUS_LOG_INF_CAT("config", "Changed menu_hotkey: 0x%02X -> 0x%02X",
                           static_cast<unsigned>(previous.menuHotkey), static_cast<unsigned>(menuHotkey));
    }
    if (previous.language != language) {
        SVPLUS_LOG_INF_CAT("config", "Changed language: %s -> %s", previous.language.c_str(), language.c_str());
    }
    diffFloat("menu_x", previous.menuX, menuX);
    diffFloat("menu_y", previous.menuY, menuY);
    diffFloat("menu_w", previous.menuW, menuW);
    diffFloat("menu_h", previous.menuH, menuH);
    diffBool("show_speaker_list", previous.showSpeakerList, showSpeakerList);
    diffBool("speaker_list_header", previous.speakerListHeader, speakerListHeader);
    diffBool("speaker_list_bg", previous.speakerListBg, speakerListBg);
    diffInt("speaker_list_bg_opacity", previous.speakerListBgOpacity, speakerListBgOpacity);
    diffInt("speaker_list_max", previous.speakerListMax, speakerListMax);
    diffFloat("speaker_list_scale", previous.speakerListScale, speakerListScale);
    diffFloat("speaker_list_x", previous.speakerListX, speakerListX);
    diffFloat("speaker_list_y", previous.speakerListY, speakerListY);
    diffBool("show_micro_icon", previous.showMicroIcon, showMicroIcon);
    diffFloat("micro_icon_scale", previous.microIconScale, microIconScale);
    diffFloat("micro_icon_x", previous.microIconX, microIconX);
    diffFloat("micro_icon_y", previous.microIconY, microIconY);
    diffBool("show_overhead_icons", previous.showOverheadIcons, showOverheadIcons);
    diffInt("overhead_icon_bg_opacity", previous.overheadIconBgOpacity, overheadIconBgOpacity);
    diffInt("overhead_icon_height", previous.overheadIconHeight, overheadIconHeight);
    diffFloat("speaker_icon_scale", previous.speakerIconScale, speakerIconScale);
    diffBool("sound_enable", previous.soundEnable, soundEnable);
    diffInt("sound_volume", previous.soundVolume, soundVolume);
    diffBool("sound_balancer", previous.soundBalancer, soundBalancer);
    diffBool("sound_filter", previous.soundFilter, soundFilter);
    diffBool("micro_enable", previous.microEnable, microEnable);
    diffInt("micro_volume", previous.microVolume, microVolume);
    if (previous.microDevice != microDevice) {
        SVPLUS_LOG_INF_CAT("config", "Changed micro_device: '%s' -> '%s'",
                           previous.microDevice.empty() ? "<default>" : previous.microDevice.c_str(),
                           microDevice.empty() ? "<default>" : microDevice.c_str());
    }
    diffBool("micro_noise_suppress", previous.microNoiseSuppress, microNoiseSuppress);
    if (previous.talkMode != talkMode) {
        SVPLUS_LOG_INF_CAT("config", "Changed talk_mode: %s -> %s",
                           previous.talkMode == TalkMode::Toggle ? "toggle" : "push",
                           talkMode == TalkMode::Toggle ? "toggle" : "push");
    }
    for (const auto& prev : previous.blacklistEntries) {
        const BlacklistEntry* cur = findOriginEntry(prev.playerName, prev.serverEndpoint);
        if (!cur) {
            SVPLUS_LOG_INF_CAT("config", "Blacklist saved removal: %s | %s | %s | %s",
                               prev.playerName.c_str(), prev.serverName.c_str(),
                               prev.serverEndpoint.c_str(), scopeToString(prev.scope));
        } else if (cur->scope != prev.scope) {
            SVPLUS_LOG_INF_CAT("config", "Blacklist saved scope change: %s | %s | %s %s -> %s",
                               cur->playerName.c_str(), cur->serverName.c_str(),
                               cur->serverEndpoint.c_str(), scopeToString(prev.scope),
                               scopeToString(cur->scope));
        }
    }
    for (const auto& cur : blacklistEntries) {
        if (!previous.findOriginEntry(cur.playerName, cur.serverEndpoint)) {
            SVPLUS_LOG_INF_CAT("config", "Blacklist saved addition: %s | %s | %s | %s",
                               cur.playerName.c_str(), cur.serverName.c_str(),
                               cur.serverEndpoint.c_str(), scopeToString(cur.scope));
        }
    }
}

std::string formatVirtualKeyName(std::uint32_t vk) noexcept {
    if (vk >= 0x70 && vk <= 0x87) { // F1 .. F24
        return "F" + std::to_string(vk - 0x70 + 1);
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= 0x60 && vk <= 0x69) { // Numpad 0..9
        return "Num " + std::to_string(vk - 0x60);
    }

    switch (vk) {
        case 0x04: return "Mouse Mid";
        case 0x05: return "Mouse X1";
        case 0x06: return "Mouse X2";
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x13: return "Pause";
        case 0x14: return "CapsLock";
        case 0x20: return "Space";
        case 0x21: return "PageUp";
        case 0x22: return "PageDown";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0xA0: return "LShift";
        case 0xA1: return "RShift";
        case 0xA2: return "LCtrl";
        case 0xA3: return "RCtrl";
        case 0xA4: return "LAlt";
        case 0xA5: return "RAlt";
        case 0xC0: return "` / ~";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "Key 0x%02X", static_cast<unsigned>(vk));
            return std::string(buf);
        }
    }
}

float resolveHudAxisFraction(float& coord, float displayExtent, float defaultFraction, bool& dirty) noexcept {
    if (coord < 0.0f) {
        coord = defaultFraction;
        dirty = true;
    } else if (coord > 1.0f && displayExtent > 1.0f) {
        coord = std::clamp(coord / displayExtent, 0.0f, 1.0f);
        dirty = true;
    }
    return std::clamp(coord, 0.0f, 1.0f);
}

} // namespace svplus::client::config
