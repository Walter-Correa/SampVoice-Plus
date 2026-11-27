/*
 * SampVoice Plus - Thread-Safe Structured Logger
 */

#pragma once

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace svplus::log {

enum class Level {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

using LogSinkFn = std::function<void(Level, const char*)>;

struct ClientSessionOptions {
    std::string dirUtf8{};
    std::string version{};
    std::string sampBanner{};
    std::string selfSha256{};
    std::string sha256FailReason{};
};

class Logger {
public:
    static constexpr std::size_t kMaxQueuedSinkEntries = 1000u;
    static constexpr std::size_t kMaxAsyncQueueLines   = 8192u;
    static constexpr std::size_t kMaxSessionBytes      = 32u * 1024u * 1024u;

    static void Init(const std::string& filePath = "", Level minLevel = Level::Info);
    static void Shutdown();

    static void InitClientSession(const ClientSessionOptions& opts, Level minLevel = Level::Info);
    static void ShutdownClientSession(bool fromExitProcess) noexcept;

    static void SetLevel(Level minLevel);
    static void SetMainThread();
    static void SetSink(LogSinkFn sink);
    static LogSinkFn ExchangeSink(LogSinkFn sink);
    static void FlushSink();

    static void Log(Level level, const char* fmt, ...);
    static void LogCat(Level level, const char* category, const char* fmt, ...);
    static void LogRawBlock(Level level, const char* category, const std::string& multilineBlock);

    [[nodiscard]] static std::uint32_t SessionErrorCount() noexcept;

private:
    struct State;
    static State& state() noexcept;
    static void LogVa(Level level, const char* category, const char* fmt, std::va_list args);
    static void WriterThreadFunc();
};

} // namespace svplus::log

#define SVPLUS_LOG_DEBUG(...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Debug, "core", __VA_ARGS__)
#define SVPLUS_LOG_INFO(...)  ::svplus::log::Logger::LogCat(::svplus::log::Level::Info,  "core", __VA_ARGS__)
#define SVPLUS_LOG_WARN(...)  ::svplus::log::Logger::LogCat(::svplus::log::Level::Warn,  "core", __VA_ARGS__)
#define SVPLUS_LOG_ERROR(...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Error, "core", __VA_ARGS__)

#define SVPLUS_LOG_DBG_CAT(cat, ...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Debug, cat, __VA_ARGS__)
#define SVPLUS_LOG_INF_CAT(cat, ...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Info,  cat, __VA_ARGS__)
#define SVPLUS_LOG_WRN_CAT(cat, ...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Warn,  cat, __VA_ARGS__)
#define SVPLUS_LOG_ERR_CAT(cat, ...) ::svplus::log::Logger::LogCat(::svplus::log::Level::Error, cat, __VA_ARGS__)
