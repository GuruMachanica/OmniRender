// filepath: tests/gpu/validation/DebugLogger.h
#pragma once

#include <string>
#include <fstream>
#include <mutex>

namespace omnirender::test::gpu {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class DebugLogger {
public:
    static DebugLogger& Instance();

    void SetLogFile(const std::string& filepath);
    void SetConsoleOutput(bool enable);
    void SetMinLogLevel(LogLevel level);

    void Log(LogLevel level, const char* format, ...);
    void Debug(const char* format, ...);
    void Info(const char* format, ...);
    void Warn(const char* format, ...);
    void Error(const char* format, ...);

    void Flush();
    void Close();

private:
    DebugLogger();
    ~DebugLogger();

    DebugLogger(const DebugLogger&) = delete;
    DebugLogger& operator=(const DebugLogger&) = delete;

    std::string GetTimestampString() const;
    const char* LevelToString(LogLevel level) const noexcept;

    std::mutex    mutex_;
    std::ofstream file_stream_;
    bool          console_enabled_ = true;
    LogLevel      min_level_ = LogLevel::Debug;
};

}  // namespace omnirender::test::gpu
