/*
 * SampVoice Plus - Thread-Safe Structured Logger Implementation
 */

#include "svplus/common/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace svplus::log {

namespace {

const char* LevelTag(Level level) noexcept {
    switch (level) {
        case Level::Debug: return "DBG";
        case Level::Info:  return "INF";
        case Level::Warn:  return "WRN";
        case Level::Error: return "ERR";
    }
    return "UNK";
}

std::tm localTimeNow() noexcept {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &nowTime);
#else
    localtime_r(&nowTime, &tmBuf);
#endif
    return tmBuf;
}

void formatTimestampDMYHMS(const std::tm& tmBuf, char* out, std::size_t outCap) noexcept {
    if (!out || outCap == 0) {
        return;
    }
    if (std::strftime(out, outCap, "%d/%m/%Y - %H:%M:%S", &tmBuf) == 0) {
        out[0] = '\0';
    }
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), needed);
    return wide;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

bool writeAllWin32(HANDLE hFile, const char* data, std::size_t len, std::size_t& bytesAdded) noexcept {
    bytesAdded = 0;
    if (hFile == INVALID_HANDLE_VALUE || !data || len == 0) {
        return false;
    }
    std::size_t offset = 0;
    while (offset < len) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(len - offset, 65536u));
        DWORD written = 0;
        if (!WriteFile(hFile, data + offset, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
        bytesAdded += written;
    }
    return true;
}

std::uint32_t findNextErrorIndex(const std::filesystem::path& dir, const std::wstring& wVer) {
    std::uint32_t maxIdx = 0;
    const std::wstring prefix = L"[ERROR] sampvoice_plus_v" + wVer + L"_log_";
    const std::wstring suffix = L".txt";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::wstring fname = entry.path().filename().wstring();
        if (fname.size() <= prefix.size() + suffix.size()) {
            continue;
        }
        if (fname.compare(0, prefix.size(), prefix) != 0 ||
            fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        const std::wstring numStr = fname.substr(prefix.size(), fname.size() - prefix.size() - suffix.size());
        wchar_t* endPtr = nullptr;
        const unsigned long val = std::wcstoul(numStr.c_str(), &endPtr, 10);
        if (endPtr && *endPtr == L'\0' && val > maxIdx) {
            maxIdx = static_cast<std::uint32_t>(val);
        }
    }
    return maxIdx + 1u;
}

enum class PreviousLogStatus {
    Clean,
    Empty,
    Unclosed
};

PreviousLogStatus inspectPreviousSessionLog(const std::filesystem::path& logPath) {
    HANDLE h = CreateFileW(
        logPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // File exists (checked by caller) but cannot be read to prove a clean closing line:
        // treat as unclosed so InitClientSession archives or appends rather than truncating it.
        return PreviousLogStatus::Unclosed;
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(h, &fileSize)) {
        CloseHandle(h);
        return PreviousLogStatus::Unclosed;
    }
    if (fileSize.QuadPart == 0) {
        // An empty 0-byte log has nothing to preserve; remove/overwrite without [ERROR].
        CloseHandle(h);
        return PreviousLogStatus::Empty;
    }
    constexpr LONGLONG kTailBytes = 2048;
    const LONGLONG startOff = std::max<LONGLONG>(0, fileSize.QuadPart - kTailBytes);
    LARGE_INTEGER seekPos{};
    seekPos.QuadPart = startOff;
    SetFilePointerEx(h, seekPos, nullptr, FILE_BEGIN);
    std::string buf(static_cast<std::size_t>(fileSize.QuadPart - startOff), '\0');
    DWORD readBytes = 0;
    const BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &readBytes, nullptr);
    CloseHandle(h);
    if (!ok || readBytes == 0) {
        return PreviousLogStatus::Unclosed;
    }
    buf.resize(readBytes);
    // Inspect last non-empty line
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' || buf.back() == ' ')) {
        buf.pop_back();
    }
    if (buf.empty()) {
        return PreviousLogStatus::Empty;
    }
    const std::size_t lastNl = buf.find_last_of('\n');
    const std::string lastLine = (lastNl == std::string::npos) ? buf : buf.substr(lastNl + 1);
    const bool endedCleanly =
        lastLine.find("] [SampVoice Plus] : Closing the SampVoice Plus v") != std::string::npos &&
        lastLine.find("clean exit,") != std::string::npos;
    return endedCleanly ? PreviousLogStatus::Clean : PreviousLogStatus::Unclosed;
}

std::wstring sessionLogFileName(const std::wstring& wVer, std::uint32_t slot) {
    if (slot <= 1) {
        return L"sampvoice_plus_v" + wVer + L"_log.txt";
    }
    return L"sampvoice_plus_v" + wVer + L"_log (" + std::to_wstring(slot) + L").txt";
}

std::wstring sessionPidFallbackFileName(const std::wstring& wVer, std::uint32_t pid) {
    return L"sampvoice_plus_v" + wVer + L"_log (pid " + std::to_wstring(pid) + L").txt";
}

std::wstring sessionSlotMutexName(const std::wstring& wVer, std::uint32_t slot) {
    if (slot <= 1) {
        return L"Local\\SampVoicePlus_SessionLog_v" + wVer;
    }
    return L"Local\\SampVoicePlus_SessionLog_v" + wVer + L"_" + std::to_wstring(slot);
}

std::wstring sessionPidMutexName(const std::wstring& wVer, std::uint32_t pid) {
    return L"Local\\SampVoicePlus_SessionLog_v" + wVer + L"_pid_" + std::to_wstring(pid);
}

bool parseStrictDecimalUint32(const std::wstring& s, std::uint32_t& outVal) noexcept {
    if (s.empty()) {
        return false;
    }
    for (wchar_t ch : s) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
    }
    wchar_t* endPtr = nullptr;
    const unsigned long val = std::wcstoul(s.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != L'\0') {
        return false;
    }
    outVal = static_cast<std::uint32_t>(val);
    return true;
}

constexpr std::size_t kMaxArchivedErrorLogs = 10u;

bool isArchivedErrorLogFileName(const std::wstring& fn) noexcept {
    constexpr const wchar_t kErrPrefix[] = L"[ERROR] sampvoice_plus_v";
    constexpr std::size_t kErrPrefLen = 24u;
    constexpr const wchar_t kLogMarker[] = L"_log_";
    constexpr std::size_t kLogMarkerLen = 5u;
    constexpr const wchar_t kSuffix[] = L".txt";
    constexpr std::size_t kSuffixLen = 4u;

    if (fn.size() <= kErrPrefLen + kLogMarkerLen + kSuffixLen) {
        return false;
    }
    if (fn.compare(0, kErrPrefLen, kErrPrefix) != 0 ||
        fn.compare(fn.size() - kSuffixLen, kSuffixLen, kSuffix) != 0) {
        return false;
    }
    const std::size_t stemEnd = fn.size() - kSuffixLen;
    const std::size_t markerPos = fn.rfind(kLogMarker, stemEnd - kLogMarkerLen);
    if (markerPos == std::wstring::npos || markerPos <= kErrPrefLen) {
        return false;
    }
    const std::wstring verStr = fn.substr(kErrPrefLen, markerPos - kErrPrefLen);
    if (verStr.empty() || verStr.find_first_of(L" \t\\/") != std::wstring::npos) {
        return false;
    }
    const std::wstring numStr = fn.substr(markerPos + kLogMarkerLen, stemEnd - (markerPos + kLogMarkerLen));
    std::uint32_t errIdx = 0;
    return parseStrictDecimalUint32(numStr, errIdx) && errIdx >= 1u;
}

struct ArchivedErrorLogEntry {
    std::filesystem::path path;
    std::wstring          fileName;
    std::uint64_t         lastWriteTime{0};
    bool                  archivedThisInit{false};
};

bool containsIgnoreCaseFileName(const std::vector<std::wstring>& names, const std::wstring& fn) noexcept {
    for (const auto& candidate : names) {
        if (_wcsicmp(candidate.c_str(), fn.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void pruneOldArchivedErrorLogs(
    const std::filesystem::path&     dirPath,
    const std::vector<std::wstring>& justArchivedFileNames,
    std::vector<std::string>&        deferredStartupWarns) {
    std::vector<ArchivedErrorLogEntry> entries;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::wstring fn = entry.path().filename().wstring();
        if (!isArchivedErrorLogFileName(fn)) {
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        std::uint64_t mtime = 0;
        if (GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &fad) != FALSE) {
            mtime = (static_cast<std::uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                    static_cast<std::uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
        }
        entries.push_back(ArchivedErrorLogEntry{
            entry.path(),
            fn,
            mtime,
            containsIgnoreCaseFileName(justArchivedFileNames, fn)
        });
    }
    if (entries.size() <= kMaxArchivedErrorLogs) {
        return;
    }
    std::sort(entries.begin(), entries.end(),
              [](const ArchivedErrorLogEntry& a, const ArchivedErrorLogEntry& b) {
                  if (a.archivedThisInit != b.archivedThisInit) {
                      return a.archivedThisInit; // Count logs archived on this startup first among the 10 kept
                  }
                  if (a.lastWriteTime != b.lastWriteTime) {
                      return a.lastWriteTime > b.lastWriteTime; // Newest first
                  }
                  return a.fileName > b.fileName;
              });
    for (std::size_t i = kMaxArchivedErrorLogs; i < entries.size(); ++i) {
        if (entries[i].archivedThisInit) {
            continue; // Never delete a crash log that was archived during this initialization
        }
        if (DeleteFileW(entries[i].path.c_str()) == FALSE) {
            const DWORD delErr = GetLastError();
            char wbuf[512]{};
            std::snprintf(wbuf, sizeof(wbuf),
                          "Failed to delete old error log \"%s\" (err=%lu)",
                          wideToUtf8(entries[i].fileName).c_str(),
                          static_cast<unsigned long>(delErr));
            deferredStartupWarns.emplace_back(wbuf);
        }
    }
}

HANDLE tryAcquireSlotMutex(const std::wstring& mutexName) noexcept {
    HANDLE hMtx = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!hMtx) {
        return INVALID_HANDLE_VALUE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMtx);
        return INVALID_HANDLE_VALUE;
    }
    return hMtx;
}

bool isSlotMutexHeldElsewhere(const std::wstring& mutexName) noexcept {
    HANDLE hProbe = OpenMutexW(SYNCHRONIZE, FALSE, mutexName.c_str());
    if (hProbe) {
        CloseHandle(hProbe);
        return true;
    }
    // Wait: ERROR_ACCESS_DENIED also proves the mutex object exists in another security context.
    return GetLastError() == ERROR_ACCESS_DENIED;
}

bool tryArchiveUnclosedLog(
    const std::filesystem::path& srcPath,
    const std::filesystem::path& dirPath,
    const std::wstring&          wVer,
    std::string&                 outErrNameUtf8,
    DWORD&                       outLastErr,
    std::wstring*                outErrFileName = nullptr) {
    outErrNameUtf8.clear();
    if (outErrFileName) {
        outErrFileName->clear();
    }
    outLastErr = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::uint32_t nextN = findNextErrorIndex(dirPath, wVer);
        const std::wstring errName =
            L"[ERROR] sampvoice_plus_v" + wVer + L"_log_" + std::to_wstring(nextN) + L".txt";
        const std::filesystem::path errPath = dirPath / errName;
        if (MoveFileExW(srcPath.c_str(), errPath.c_str(), 0) != FALSE ||
            CopyFileW(srcPath.c_str(), errPath.c_str(), TRUE) != FALSE) {
            outErrNameUtf8 = wideToUtf8(errName);
            if (outErrFileName) {
                *outErrFileName = errName;
            }
            return true;
        }
        outLastErr = GetLastError();
        if (outLastErr != ERROR_FILE_EXISTS && outLastErr != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    return false;
}
#endif

} // namespace

struct QueuedAsyncLine {
    Level       level{Level::Info};
    std::string timestamp;
    std::string body; // "[TAG:cat] : message"
};

struct Logger::State {
    std::mutex mutex;
    std::condition_variable cv;
    std::FILE* file{nullptr};
    std::atomic<Level> minLevel{Level::Info};
    LogSinkFn sink{};
    std::thread::id mainThreadId{};
    bool hasMainThread{false};
    std::deque<std::pair<Level, std::string>> queuedSinkLogs{};
    std::size_t droppedQueued{0};

    // Client session async Win32 logging state
    bool sessionActive{false};
    bool stopWriter{false};
    bool urgentFlush{false};
    bool capReached{false};
    std::filesystem::path sessionDir{};
    std::filesystem::path sessionLogPath{};
    std::string sessionVersion{};
    std::deque<QueuedAsyncLine> asyncQueue{};
    std::size_t droppedAsync{0};
    std::string lastCoalesceTimestamp{};
    std::string lastCoalesceBody{};
    Level lastCoalesceLevel{Level::Info};
    std::size_t repeatCount{0};
    std::size_t writtenBytes{0};
    std::atomic<std::uint32_t> errorCount{0};
    std::atomic<bool> writerBusy{false};
    std::thread* writerThread{nullptr};
#ifdef _WIN32
    HANDLE winFile{INVALID_HANDLE_VALUE};
    HANDLE sessionMutex{INVALID_HANDLE_VALUE};
#endif
};

Logger::State& Logger::state() noexcept {
    // Heap-allocate intentionally so process teardown (ExitProcess) never runs mutex/deque destructors
    // while background threads are still active or logging.
    static auto* s = new State();
    return *s;
}

std::uint32_t Logger::SessionErrorCount() noexcept {
    return state().errorCount.load(std::memory_order_acquire);
}

void Logger::Init(const std::string& filePath, Level minLevel) {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    st.minLevel.store(minLevel, std::memory_order_release);

    if (st.file) {
        std::fclose(st.file);
        st.file = nullptr;
    }

    if (!filePath.empty()) {
        st.file = std::fopen(filePath.c_str(), "a");
    }
}

void Logger::InitClientSession(const ClientSessionOptions& opts, Level minLevel) {
#ifdef _WIN32
    auto& st = state();
    st.minLevel.store(minLevel, std::memory_order_release);
    st.errorCount.store(0, std::memory_order_release);

    const std::wstring wDir = utf8ToWide(opts.dirUtf8);
    const std::wstring wVer = utf8ToWide(opts.version);
    const std::filesystem::path dirPath(wDir);
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);

    // Claim per-session-log user mutex so concurrent GTA processes never collide on or truncate
    // another live process's session log. Slot 1 owns the primary log and performs startup cleanup.
    if (st.sessionMutex != INVALID_HANDLE_VALUE) {
        CloseHandle(st.sessionMutex);
        st.sessionMutex = INVALID_HANDLE_VALUE;
    }
    std::uint32_t chosenSlot = 1;
    bool usePidFallback = false;
    const std::uint32_t currentPid = static_cast<std::uint32_t>(GetCurrentProcessId());
    HANDLE acquiredMutex = INVALID_HANDLE_VALUE;
    for (std::uint32_t slot = 1; slot <= 64u; ++slot) {
        acquiredMutex = tryAcquireSlotMutex(sessionSlotMutexName(wVer, slot));
        if (acquiredMutex != INVALID_HANDLE_VALUE) {
            chosenSlot = slot;
            break;
        }
    }
    if (acquiredMutex == INVALID_HANDLE_VALUE) {
        usePidFallback = true;
        acquiredMutex = tryAcquireSlotMutex(sessionPidMutexName(wVer, currentPid));
    }
    const bool isPrimaryOwner = (!usePidFallback && chosenSlot == 1);

    const std::wstring curFileName =
        usePidFallback ? sessionPidFallbackFileName(wVer, currentPid)
                       : sessionLogFileName(wVer, chosenSlot);
    const std::filesystem::path logPath = dirPath / curFileName;

    std::vector<std::string> deferredStartupWarns;
    std::vector<std::wstring> justArchivedFileNames;
    std::string archivedNameUtf8;
    bool appendUnarchivedSession = false;
    DWORD appendArchiveErr = 0;

    // 1. Check previous log of our own slot for unclosed termination before truncating
    if (std::filesystem::exists(logPath, ec) && std::filesystem::is_regular_file(logPath, ec)) {
        const PreviousLogStatus prevStatus = inspectPreviousSessionLog(logPath);
        if (prevStatus == PreviousLogStatus::Empty) {
            static_cast<void>(DeleteFileW(logPath.c_str()));
        } else if (prevStatus == PreviousLogStatus::Unclosed) {
            std::wstring archivedErrWide;
            if (tryArchiveUnclosedLog(logPath, dirPath, wVer, archivedNameUtf8, appendArchiveErr, &archivedErrWide)) {
                justArchivedFileNames.push_back(std::move(archivedErrWide));
            } else {
                appendUnarchivedSession = true;
            }
        }
    }

    // 2. Cleanup stale other-version logs and dead secondary-slot logs
    //    STRICTLY in the primary session owner so concurrent live processes are never disrupted.
    if (isPrimaryOwner) {
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::wstring fn = entry.path().filename().wstring();
            constexpr const wchar_t kPrefix[] = L"sampvoice_plus_v";
            constexpr std::size_t kPrefLen = 16;
            if (fn.size() <= kPrefLen || fn.compare(0, kPrefLen, kPrefix) != 0 || fn == curFileName) {
                continue;
            }
            const std::size_t logMarkerPos = fn.find(L"_log", kPrefLen);
            if (logMarkerPos == std::wstring::npos) {
                continue;
            }
            const std::wstring entryVer = fn.substr(kPrefLen, logMarkerPos - kPrefLen);
            const std::wstring rest = fn.substr(logMarkerPos);

            std::wstring entryMutexName;
            if (rest == L"_log.txt") {
                entryMutexName = sessionSlotMutexName(entryVer, 1);
            } else if (rest.size() > 11 && rest.compare(0, 6, L"_log (") == 0 &&
                       rest.compare(rest.size() - 5, 5, L").txt") == 0) {
                const std::wstring inner = rest.substr(6, rest.size() - 11);
                if (inner.size() > 4 && inner.compare(0, 4, L"pid ") == 0) {
                    std::uint32_t entryPid = 0;
                    if (parseStrictDecimalUint32(inner.substr(4), entryPid) && entryPid > 0) {
                        entryMutexName = sessionPidMutexName(entryVer, entryPid);
                    }
                } else {
                    std::uint32_t entrySlot = 0;
                    if (parseStrictDecimalUint32(inner, entrySlot) && entrySlot >= 2) {
                        entryMutexName = sessionSlotMutexName(entryVer, entrySlot);
                    }
                }
            }
            if (entryMutexName.empty()) {
                continue;
            }

            if (isSlotMutexHeldElsewhere(entryMutexName)) {
                continue;
            }

            const PreviousLogStatus prevStatus = inspectPreviousSessionLog(entry.path());
            if (prevStatus == PreviousLogStatus::Unclosed) {
                std::string archivedErrName;
                std::wstring archivedErrWide;
                DWORD archiveErr = 0;
                if (tryArchiveUnclosedLog(entry.path(), dirPath, entryVer, archivedErrName, archiveErr, &archivedErrWide)) {
                    justArchivedFileNames.push_back(std::move(archivedErrWide));
                    char wbuf[512]{};
                    if (entryVer == wVer) {
                        std::snprintf(wbuf, sizeof(wbuf),
                                      "Previous secondary session (%s) did not close cleanly; its log was kept as \"%s\"",
                                      wideToUtf8(fn).c_str(), archivedErrName.c_str());
                    } else {
                        std::snprintf(wbuf, sizeof(wbuf),
                                      "Previous v%s session (%s) did not close cleanly; its log was kept as \"%s\"",
                                      wideToUtf8(entryVer).c_str(), wideToUtf8(fn).c_str(), archivedErrName.c_str());
                    }
                    deferredStartupWarns.emplace_back(wbuf);
                } else {
                    char wbuf[512]{};
                    if (entryVer == wVer) {
                        std::snprintf(wbuf, sizeof(wbuf),
                                      "Failed to archive unclosed secondary session log \"%s\" (err=%lu)",
                                      wideToUtf8(fn).c_str(), static_cast<unsigned long>(archiveErr));
                    } else {
                        std::snprintf(wbuf, sizeof(wbuf),
                                      "Failed to archive unclosed v%s session log \"%s\" (err=%lu)",
                                      wideToUtf8(entryVer).c_str(), wideToUtf8(fn).c_str(), static_cast<unsigned long>(archiveErr));
                    }
                    deferredStartupWarns.emplace_back(wbuf);
                }
            } else {
                if (DeleteFileW(entry.path().c_str()) == FALSE && entryVer != wVer) {
                    const DWORD delErr = GetLastError();
                    char wbuf[512]{};
                    std::snprintf(wbuf, sizeof(wbuf),
                                  "Failed to delete old version log \"%s\" (err=%lu)",
                                  wideToUtf8(fn).c_str(), static_cast<unsigned long>(delErr));
                    deferredStartupWarns.emplace_back(wbuf);
                }
            }
        }

        // Retain only the 10 most recent [ERROR] logs across all versions (keeping any archived on this init)
        pruneOldArchivedErrorLogs(dirPath, justArchivedFileNames, deferredStartupWarns);
    }

    // 3. Open session log via Win32 HANDLE (CRT lock-free safe exit); never truncate an unarchivable crash log
    HANDLE hFile = CreateFileW(
        logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        appendUnarchivedSession ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    std::size_t initialBytes = 0;
    if (hFile != INVALID_HANDLE_VALUE) {
        if (appendUnarchivedSession) {
            LARGE_INTEGER existingSize{};
            if (GetFileSizeEx(hFile, &existingSize) != FALSE && existingSize.QuadPart > 0) {
                initialBytes = static_cast<std::size_t>(existingSize.QuadPart);
            }
            LARGE_INTEGER zero{};
            SetFilePointerEx(hFile, zero, nullptr, FILE_END);
            char sepBuf[160]{};
            const int nSep = std::snprintf(
                sepBuf, sizeof(sepBuf),
                "%s---- new session (previous log could not be archived: err=%lu) ----\r\n",
                initialBytes > 0 ? "\r\n" : "",
                static_cast<unsigned long>(appendArchiveErr));
            if (nSep > 0) {
                std::size_t add = 0;
                writeAllWin32(hFile, sepBuf, static_cast<std::size_t>(nSep), add);
                initialBytes += add;
            }
        }
        const std::tm tmBuf = localTimeNow();
        char ts[32]{};
        formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));

        const char* hashStr = opts.selfSha256.empty() ? "unavailable" : opts.selfSha256.c_str();
        char line1[640]{};
        const int n1 = std::snprintf(
            line1, sizeof(line1),
            "[%s] [SampVoice Plus] : Initializing %s + SampVoice Plus v%s log (Hash: %s)\r\n",
            ts, opts.sampBanner.c_str(), opts.version.c_str(), hashStr);
        if (n1 > 0) {
            std::size_t add = 0;
            writeAllWin32(hFile, line1, static_cast<std::size_t>(n1), add);
            initialBytes += add;
        }
        if (opts.selfSha256.empty() && !opts.sha256FailReason.empty()) {
            char lineHashWarn[512]{};
            const int nw = std::snprintf(
                lineHashWarn, sizeof(lineHashWarn),
                "[%s] [WRN:core] : Unable to compute sampvoice.asi SHA-256: %s\r\n",
                ts, opts.sha256FailReason.c_str());
            if (nw > 0) {
                std::size_t add = 0;
                writeAllWin32(hFile, lineHashWarn, static_cast<std::size_t>(nw), add);
                initialBytes += add;
            }
        }
        if (!archivedNameUtf8.empty()) {
            char line2[512]{};
            const int n2 = std::snprintf(
                line2, sizeof(line2),
                "[%s] [WRN:core] : Previous session did not close cleanly; its log was kept as \"%s\"\r\n",
                ts, archivedNameUtf8.c_str());
            if (n2 > 0) {
                std::size_t add = 0;
                writeAllWin32(hFile, line2, static_cast<std::size_t>(n2), add);
                initialBytes += add;
            }
        }
        for (const auto& warnMsg : deferredStartupWarns) {
            char wline[512]{};
            const int nw = std::snprintf(wline, sizeof(wline), "[%s] [WRN:core] : %s\r\n", ts, warnMsg.c_str());
            if (nw > 0) {
                std::size_t add = 0;
                writeAllWin32(hFile, wline, static_cast<std::size_t>(nw), add);
                initialBytes += add;
            }
        }
        FlushFileBuffers(hFile);
    }

    {
        std::lock_guard lock(st.mutex);
        st.winFile = hFile;
        st.sessionMutex = acquiredMutex;
        st.sessionDir = dirPath;
        st.sessionLogPath = logPath;
        st.sessionVersion = opts.version;
        st.writtenBytes = initialBytes;
        st.capReached = false;
        st.errorCount.store(0, std::memory_order_release);
        st.stopWriter = false;
        st.urgentFlush = false;
        st.asyncQueue.clear();
        st.droppedAsync = 0;
        st.lastCoalesceBody.clear();
        st.repeatCount = 0;
        st.writerBusy.store(false, std::memory_order_release);
        st.sessionActive = (hFile != INVALID_HANDLE_VALUE);
        if (st.sessionActive && !st.writerThread) {
            st.writerThread = new std::thread(&Logger::WriterThreadFunc);
        }
    }
#else
    (void)opts;
    (void)minLevel;
#endif
}

#ifdef _WIN32
namespace {

void emitRepeatedNoticeWin32(
    HANDLE hFile,
    std::size_t& writtenBytes,
    const std::string& ts,
    Level lvl,
    const std::string& body,
    std::size_t& repeatCount) noexcept {
    if (repeatCount == 0 || hFile == INVALID_HANDLE_VALUE) {
        repeatCount = 0;
        return;
    }
    // Extract category out of "[TAG:cat] : msg"
    std::string cat = "core";
    const std::size_t colon = body.find(':');
    const std::size_t rbr   = body.find(']');
    if (colon != std::string::npos && rbr != std::string::npos && colon < rbr) {
        cat = body.substr(colon + 1, rbr - colon - 1);
    }
    char buf[256]{};
    const int n = std::snprintf(
        buf, sizeof(buf),
        "[%s] [%s:%s] : last message repeated %zu time(s)\r\n",
        ts.c_str(), LevelTag(lvl), cat.c_str(), repeatCount);
    repeatCount = 0;
    if (n > 0) {
        std::size_t add = 0;
        writeAllWin32(hFile, buf, static_cast<std::size_t>(n), add);
        writtenBytes += add;
    }
}

void writeFormattedLineWin32(
    HANDLE hFile,
    std::size_t& writtenBytes,
    bool& capReached,
    std::atomic<std::uint32_t>& errorCount,
    const QueuedAsyncLine& item) noexcept {
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    if (capReached && item.level != Level::Warn && item.level != Level::Error) {
        return;
    }

    std::string fullLine;
    fullLine.reserve(item.timestamp.size() + item.body.size() + 6);
    fullLine.push_back('[');
    fullLine.append(item.timestamp);
    fullLine.append("] ");
    fullLine.append(item.body);
    fullLine.append("\r\n");

    std::size_t add = 0;
    writeAllWin32(hFile, fullLine.data(), fullLine.size(), add);
    writtenBytes += add;

    if (!capReached && writtenBytes >= Logger::kMaxSessionBytes) {
        capReached = true;
        errorCount.fetch_add(1, std::memory_order_acq_rel);
        char capMsg[256]{};
        const int nc = std::snprintf(
            capMsg, sizeof(capMsg),
            "[%s] [ERR:core] : Session log reached 32 MB ceiling; suppressing Info/Debug lines\r\n",
            item.timestamp.c_str());
        if (nc > 0) {
            std::size_t capAdd = 0;
            writeAllWin32(hFile, capMsg, static_cast<std::size_t>(nc), capAdd);
            writtenBytes += capAdd;
        }
    }
}

void writeQueuedLineWin32(
    HANDLE hFile,
    std::size_t& writtenBytes,
    bool& capReached,
    std::atomic<std::uint32_t>& errorCount,
    std::string& lastTs,
    std::string& lastBody,
    Level& lastLvl,
    std::size_t& repeatCount,
    const QueuedAsyncLine& item) {
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    if (capReached && item.level != Level::Warn && item.level != Level::Error) {
        return;
    }
    if (!lastBody.empty() && item.body == lastBody) {
        lastTs = item.timestamp;
        ++repeatCount;
        return;
    }
    emitRepeatedNoticeWin32(hFile, writtenBytes, lastTs, lastLvl, lastBody, repeatCount);

    writeFormattedLineWin32(hFile, writtenBytes, capReached, errorCount, item);

    lastTs = item.timestamp;
    lastBody = item.body;
    lastLvl = item.level;
}

} // namespace
#endif

void Logger::WriterThreadFunc() {
#ifdef _WIN32
    auto& st = state();
    while (true) {
        std::deque<QueuedAsyncLine> batch;
        std::size_t dropped = 0;
        bool urgent = false;
        bool shouldStop = false;
        {
            std::unique_lock lock(st.mutex);
            st.cv.wait_for(lock, std::chrono::milliseconds(500), [&]() {
                return st.stopWriter || st.urgentFlush || !st.asyncQueue.empty();
            });
            batch.swap(st.asyncQueue);
            dropped = st.droppedAsync;
            st.droppedAsync = 0;
            urgent = st.urgentFlush;
            st.urgentFlush = false;
            shouldStop = st.stopWriter;
        }

        const bool wroteAnything = !batch.empty() || dropped > 0;
        st.writerBusy.store(true, std::memory_order_release);
        if (dropped > 0 && st.winFile != INVALID_HANDLE_VALUE) {
            const std::tm tmBuf = localTimeNow();
            char ts[32]{};
            formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));
            QueuedAsyncLine dropLine{Level::Warn, ts, "[WRN:core] : " + std::to_string(dropped) + " lines dropped (async queue limit)"};
            writeQueuedLineWin32(
                st.winFile, st.writtenBytes, st.capReached, st.errorCount,
                st.lastCoalesceTimestamp, st.lastCoalesceBody, st.lastCoalesceLevel, st.repeatCount, dropLine);
        }

        for (const auto& item : batch) {
            writeQueuedLineWin32(
                st.winFile, st.writtenBytes, st.capReached, st.errorCount,
                st.lastCoalesceTimestamp, st.lastCoalesceBody, st.lastCoalesceLevel, st.repeatCount, item);
        }

        if ((wroteAnything || urgent || shouldStop) && st.winFile != INVALID_HANDLE_VALUE) {
            if (shouldStop && st.repeatCount > 0) {
                emitRepeatedNoticeWin32(
                    st.winFile, st.writtenBytes, st.lastCoalesceTimestamp,
                    st.lastCoalesceLevel, st.lastCoalesceBody, st.repeatCount);
            }
            FlushFileBuffers(st.winFile);
        }
        st.writerBusy.store(false, std::memory_order_release);

        if (shouldStop) {
            break;
        }
    }
#endif
}

void Logger::ShutdownClientSession(bool fromExitProcess) noexcept {
#ifdef _WIN32
    auto& st = state();
    if (!st.sessionActive && st.winFile == INVALID_HANDLE_VALUE) {
        return;
    }
    st.sessionActive = false;

    if (!fromExitProcess) {
        std::thread* wt = nullptr;
        {
            std::lock_guard lock(st.mutex);
            st.stopWriter = true;
            wt = st.writerThread;
            st.writerThread = nullptr;
        }
        st.cv.notify_all();
        if (wt) {
            if (wt->joinable()) {
                wt->join();
            }
            delete wt;
        }
    } else {
        // ExitProcess: background writer thread may have been terminated holding st.mutex or
        // mid-write outside st.mutex (mutating coalesce state). Never wait or join; if
        // writerBusy is set, flush queued lines directly without touching coalesce state or repeat notices.
        const bool writerWasBusy = st.writerBusy.load(std::memory_order_acquire);
        std::unique_lock lock(st.mutex, std::try_to_lock);
        if (lock.owns_lock() && st.winFile != INVALID_HANDLE_VALUE) {
            if (writerWasBusy) {
                if (st.droppedAsync > 0) {
                    const std::tm tmBuf = localTimeNow();
                    char ts[32]{};
                    formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));
                    QueuedAsyncLine dropLine{Level::Warn, ts, "[WRN:core] : " + std::to_string(st.droppedAsync) + " lines dropped (async queue limit)"};
                    st.droppedAsync = 0;
                    writeFormattedLineWin32(st.winFile, st.writtenBytes, st.capReached, st.errorCount, dropLine);
                }
                for (const auto& item : st.asyncQueue) {
                    writeFormattedLineWin32(st.winFile, st.writtenBytes, st.capReached, st.errorCount, item);
                }
                st.asyncQueue.clear();
            } else {
                if (st.droppedAsync > 0) {
                    const std::tm tmBuf = localTimeNow();
                    char ts[32]{};
                    formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));
                    QueuedAsyncLine dropLine{Level::Warn, ts, "[WRN:core] : " + std::to_string(st.droppedAsync) + " lines dropped (async queue limit)"};
                    st.droppedAsync = 0;
                    writeQueuedLineWin32(
                        st.winFile, st.writtenBytes, st.capReached, st.errorCount,
                        st.lastCoalesceTimestamp, st.lastCoalesceBody, st.lastCoalesceLevel, st.repeatCount, dropLine);
                }
                for (const auto& item : st.asyncQueue) {
                    writeQueuedLineWin32(
                        st.winFile, st.writtenBytes, st.capReached, st.errorCount,
                        st.lastCoalesceTimestamp, st.lastCoalesceBody, st.lastCoalesceLevel, st.repeatCount, item);
                }
                st.asyncQueue.clear();
                emitRepeatedNoticeWin32(
                    st.winFile, st.writtenBytes, st.lastCoalesceTimestamp,
                    st.lastCoalesceLevel, st.lastCoalesceBody, st.repeatCount);
            }
        }
    }

    HANDLE h = st.winFile;
    st.winFile = INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        const std::uint32_t errCount = st.errorCount.load(std::memory_order_acquire);
        const std::tm tmBuf = localTimeNow();
        char ts[32]{};
        formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));
        char closingBuf[320]{};
        const int nClose = std::snprintf(
            closingBuf, sizeof(closingBuf),
            "[%s] [SampVoice Plus] : Closing the SampVoice Plus v%s log (clean exit, %u errors).\r\n",
            ts, st.sessionVersion.c_str(), static_cast<unsigned>(errCount));
        if (nClose > 0) {
            std::size_t add = 0;
            writeAllWin32(h, closingBuf, static_cast<std::size_t>(nClose), add);
        }
        FlushFileBuffers(h);
        CloseHandle(h);

        if (errCount > 0 && !st.sessionDir.empty() && !st.sessionLogPath.empty()) {
            const std::wstring wVer = utf8ToWide(st.sessionVersion);
            std::string dummyName;
            DWORD dummyErr = 0;
            static_cast<void>(tryArchiveUnclosedLog(st.sessionLogPath, st.sessionDir, wVer, dummyName, dummyErr));
        }
    }
    if (st.sessionMutex != INVALID_HANDLE_VALUE) {
        CloseHandle(st.sessionMutex);
        st.sessionMutex = INVALID_HANDLE_VALUE;
    }
#else
    (void)fromExitProcess;
#endif
}

void Logger::Shutdown() {
    auto& st = state();
    LogSinkFn sinkCopy;
    std::deque<std::pair<Level, std::string>> pending;
    std::size_t dropped = 0;
    bool drainOnThisThread = false;
    {
        std::lock_guard lock(st.mutex);
        drainOnThisThread = !st.hasMainThread || (std::this_thread::get_id() == st.mainThreadId);
        if (st.sink && drainOnThisThread) {
            sinkCopy = st.sink;
            pending.swap(st.queuedSinkLogs);
            dropped = st.droppedQueued;
        } else {
            st.queuedSinkLogs.clear();
        }
        st.droppedQueued = 0;
        st.hasMainThread = false;
        st.mainThreadId  = {};
        st.sink = nullptr;

        if (st.file) {
            std::fflush(st.file);
            std::fclose(st.file);
            st.file = nullptr;
        }
    }

    if (sinkCopy && drainOnThisThread) {
        if (dropped > 0) {
            char dropBuf[160];
            std::snprintf(dropBuf, sizeof(dropBuf),
                          "[sv+:WRN] Dropped %zu queued log line(s) (queue limit %zu)",
                          dropped, kMaxQueuedSinkEntries);
            sinkCopy(Level::Warn, dropBuf);
        }
        for (const auto& entry : pending) {
            sinkCopy(entry.first, entry.second.c_str());
        }
    }
}

void Logger::SetLevel(Level minLevel) {
    state().minLevel.store(minLevel, std::memory_order_release);
}

void Logger::SetMainThread() {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    st.mainThreadId  = std::this_thread::get_id();
    st.hasMainThread = true;
}

void Logger::SetSink(LogSinkFn sink) {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    st.sink = std::move(sink);
}

void Logger::FlushSink() {
    auto& st = state();
    LogSinkFn sinkCopy;
    std::deque<std::pair<Level, std::string>> pending;
    std::size_t dropped = 0;
    {
        std::lock_guard lock(st.mutex);
        if (!st.sink) {
            return;
        }
        if (st.hasMainThread && std::this_thread::get_id() != st.mainThreadId) {
            return;
        }
        if (st.queuedSinkLogs.empty() && st.droppedQueued == 0) {
            return;
        }
        sinkCopy = st.sink;
        pending.swap(st.queuedSinkLogs);
        dropped = st.droppedQueued;
        st.droppedQueued = 0;
    }

    if (sinkCopy) {
        if (dropped > 0) {
            char dropBuf[160];
            std::snprintf(dropBuf, sizeof(dropBuf),
                          "[sv+:WRN] Dropped %zu queued log line(s) (queue limit %zu)",
                          dropped, kMaxQueuedSinkEntries);
            sinkCopy(Level::Warn, dropBuf);
        }
        for (const auto& entry : pending) {
            sinkCopy(entry.first, entry.second.c_str());
        }
    }
}

void Logger::Log(Level level, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    LogVa(level, "core", fmt, args);
    va_end(args);
}

void Logger::LogCat(Level level, const char* category, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    LogVa(level, category, fmt, args);
    va_end(args);
}

void Logger::LogRawBlock(Level level, const char* category, const std::string& multilineBlock) {
    auto& st = state();
    if (static_cast<int>(level) < static_cast<int>(st.minLevel.load(std::memory_order_acquire))) {
        return;
    }
    const char* cat = (category && category[0] != '\0') ? category : "core";
    const std::tm tmBuf = localTimeNow();
    char ts[32]{};
    formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));

    if (level == Level::Error) {
        st.errorCount.fetch_add(1, std::memory_order_acq_rel);
    }

    std::vector<QueuedAsyncLine> lines;
    std::size_t pos = 0;
    while (pos <= multilineBlock.size()) {
        std::size_t nl = multilineBlock.find('\n', pos);
        if (nl == std::string::npos) {
            nl = multilineBlock.size();
        }
        std::string line = multilineBlock.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            char bodyBuf[1280]{};
            std::snprintf(bodyBuf, sizeof(bodyBuf), "[%s:%s] : %s", LevelTag(level), cat, line.c_str());
            lines.push_back({level, ts, bodyBuf});
        }
        if (nl >= multilineBlock.size()) {
            break;
        }
        pos = nl + 1;
    }
    if (lines.empty()) {
        return;
    }

    bool wakeWriter = false;
    {
        std::lock_guard lock(st.mutex);
        if (st.sessionActive) {
            for (auto& item : lines) {
                if (st.capReached && item.level != Level::Warn && item.level != Level::Error) {
                    continue;
                }
                if (st.asyncQueue.size() >= kMaxAsyncQueueLines) {
                    st.asyncQueue.pop_front();
                    ++st.droppedAsync;
                }
                st.asyncQueue.push_back(std::move(item));
            }
            if (level == Level::Warn || level == Level::Error) {
                st.urgentFlush = true;
            }
            wakeWriter = true;
        }
    }
    if (wakeWriter) {
        st.cv.notify_one();
    }
}

void Logger::LogVa(Level level, const char* category, const char* fmt, std::va_list args) {
    auto& st = state();
    if (!fmt || static_cast<int>(level) < static_cast<int>(st.minLevel.load(std::memory_order_acquire))) {
        return;
    }

    char msgBuf[1024]{};
    std::vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    const char* cat = (category && category[0] != '\0') ? category : "core";
    const std::tm tmBuf = localTimeNow();

    if (level == Level::Error) {
        st.errorCount.fetch_add(1, std::memory_order_acq_rel);
    }

    // Legacy server / sink format
    char sinkBuf[1152]{};
    std::snprintf(sinkBuf, sizeof(sinkBuf), "[sv+:%s] %s", LevelTag(level), msgBuf);

    char ts[32]{};
    formatTimestampDMYHMS(tmBuf, ts, sizeof(ts));

    char bodyBuf[1200]{};
    std::snprintf(bodyBuf, sizeof(bodyBuf), "[%s:%s] : %s", LevelTag(level), cat, msgBuf);

    LogSinkFn sinkCopy;
    std::deque<std::pair<Level, std::string>> pending;
    std::size_t dropped = 0;
    bool notifyAsync = false;
    {
        std::lock_guard lock(st.mutex);
        if (st.sessionActive) {
            if (!st.capReached || level == Level::Warn || level == Level::Error) {
                if (st.asyncQueue.size() >= kMaxAsyncQueueLines) {
                    st.asyncQueue.pop_front();
                    ++st.droppedAsync;
                }
                st.asyncQueue.push_back({level, ts, bodyBuf});
                if (level == Level::Warn || level == Level::Error) {
                    st.urgentFlush = true;
                    notifyAsync = true;
                } else if (st.asyncQueue.size() >= (kMaxAsyncQueueLines / 2)) {
                    notifyAsync = true;
                }
            }
        }
        if (st.file) {
            std::fprintf(st.file, "[%s] %s\n", ts, bodyBuf);
            std::fflush(st.file);
        }
        if (st.sink) {
            const bool onMainThread =
                !st.hasMainThread || (std::this_thread::get_id() == st.mainThreadId);
            if (onMainThread) {
                sinkCopy = st.sink;
                if (!st.queuedSinkLogs.empty() || st.droppedQueued > 0) {
                    pending.swap(st.queuedSinkLogs);
                    dropped = st.droppedQueued;
                    st.droppedQueued = 0;
                }
            } else {
                if (st.queuedSinkLogs.size() >= kMaxQueuedSinkEntries) {
                    st.queuedSinkLogs.pop_front();
                    ++st.droppedQueued;
                }
                st.queuedSinkLogs.emplace_back(level, std::string(sinkBuf));
            }
        }
    }

    if (notifyAsync) {
        st.cv.notify_one();
    }

    if (sinkCopy) {
        if (dropped > 0) {
            char dropBuf[160];
            std::snprintf(dropBuf, sizeof(dropBuf),
                          "[sv+:WRN] Dropped %zu queued log line(s) (queue limit %zu)",
                          dropped, kMaxQueuedSinkEntries);
            sinkCopy(Level::Warn, dropBuf);
        }
        for (const auto& entry : pending) {
            sinkCopy(entry.first, entry.second.c_str());
        }
        sinkCopy(level, sinkBuf);
    }
}

} // namespace svplus::log
