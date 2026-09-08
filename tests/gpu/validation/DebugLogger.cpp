// filepath: tests/gpu/validation/DebugLogger.cpp
#include "DebugLogger.h"
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <vector>

namespace omnirender::test::gpu {

DebugLogger& DebugLogger::Instance() {
    static DebugLogger instance;
    return instance;
}

DebugLogger::DebugLogger() {
    SetLogFile("omnirender_gpu_test.log");
}

DebugLogger::~DebugLogger() {
    Close();
}

void DebugLogger::SetLogFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
    file_stream_.open(filepath, std::ios::out | std::ios::trunc);
    if (file_stream_.is_open()) {
        file_stream_ << "=== OmniRender GPU Integration Test Log Opened ===" << std::endl;
    }
}

void DebugLogger::SetConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_enabled_ = enable;
}

void DebugLogger::SetMinLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

const char* DebugLogger::LevelToString(LogLevel level) const noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "UNKN ";
    }
}

std::string DebugLogger::GetTimestampString() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
#if defined(_WIN32)
    localtime_s(&bt, &timer);
#else
    localtime_r(&timer, &bt);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  bt.tm_year + 1900, bt.tm_mon + 1, bt.tm_mday,
                  bt.tm_hour, bt.tm_min, bt.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf);
}

void DebugLogger::Log(LogLevel level, const char* format, ...) {
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::string timestamp = GetTimestampString();
    const char* level_str = LevelToString(level);

    std::lock_guard<std::mutex> lock(mutex_);
    if (console_enabled_) {
        std::cout << "[" << timestamp << "] [" << level_str << "] " << buffer << std::endl;
    }
    if (file_stream_.is_open()) {
        file_stream_ << "[" << timestamp << "] [" << level_str << "] " << buffer << std::endl;
    }
}

void DebugLogger::Debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Log(LogLevel::Debug, "%s", buf);
}

void DebugLogger::Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Log(LogLevel::Info, "%s", buf);
}

void DebugLogger::Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Log(LogLevel::Warn, "%s", buf);
}

void DebugLogger::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Log(LogLevel::Error, "%s", buf);
}

void DebugLogger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
    std::cout.flush();
}

void DebugLogger::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_ << "=== OmniRender GPU Integration Test Log Closed ===" << std::endl;
        file_stream_.flush();
        file_stream_.close();
    }
}

}  // namespace omnirender::test::gpu
