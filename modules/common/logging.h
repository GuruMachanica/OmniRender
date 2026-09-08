// filepath: modules/common/logging.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

// Lightweight thread-safe logger. Header-only so both the hook DLL and
// the daemon executable share the same implementation without a
// separate translation unit.
namespace omnirender {

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetMinLevel(Level level) { min_level_ = level; }
    Level MinLevel() const { return min_level_; }

    void Log(Level level, const char* file, int line, const char* fmt, ...) {
        if (static_cast<int>(level) < static_cast<int>(min_level_)) {
            return;
        }
        char message[2048];
        std::va_list args;
        va_start(args, fmt);
        std::vsnprintf(message, sizeof(message), fmt, args);
        va_end(args);

        std::lock_guard<std::mutex> lock(mu_);
        const char* tag = LevelTag(level);
        std::fprintf(stderr, "[OmniRender %s] %s:%d %s\n", tag, file, line, message);
        OutputDebugStringA(message);
    }

private:
    Logger() = default;

    static const char* LevelTag(Level level) {
        switch (level) {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO ";
            case Level::Warn:  return "WARN ";
            case Level::Error: return "ERROR";
        }
        return "?    ";
    }

    Level         min_level_ = Level::Info;
    std::mutex    mu_;
};

} // namespace omnirender

#define OMNI_LOG(level, fmt, ...) \
    ::omnirender::Logger::Instance().Log(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define OMNI_LOG_DEBUG(fmt, ...) OMNI_LOG(::omnirender::Logger::Level::Debug, fmt, ##__VA_ARGS__)
#define OMNI_LOG_INFO(fmt, ...)  OMNI_LOG(::omnirender::Logger::Level::Info,  fmt, ##__VA_ARGS__)
#define OMNI_LOG_WARN(fmt, ...)  OMNI_LOG(::omnirender::Logger::Level::Warn,  fmt, ##__VA_ARGS__)
#define OMNI_LOG_ERROR(fmt, ...) OMNI_LOG(::omnirender::Logger::Level::Error, fmt, ##__VA_ARGS__)