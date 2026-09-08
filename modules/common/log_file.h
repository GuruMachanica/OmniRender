// filepath: modules/common/log_file.h
// Optional file logger with simple size-based rotation.
//
// Activated by setting the environment variable OMNIRENDER_LOG_FILE to a
// path. When the file exceeds kMaxBytesPerFile bytes, it is rotated:
//   omnirender.log      <- current
//   omnirender.log.1    <- previous
//   omnirender.log.2    <- before that
//   ...
//   omnirender.log.{kMaxFiles-1}
//   (oldest dropped)
//
// This is a v0.3.0-alpha feature. It coexists with the stderr +
// OutputDebugStringA path in logging.h.

#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace omnirender {

class LogFile {
public:
    static constexpr std::size_t kMaxBytesPerFile = 10 * 1024 * 1024;  // 10 MB
    static constexpr int         kMaxFiles         = 5;

    static LogFile& Instance() {
        static LogFile instance;
        return instance;
    }

    // Set the log file path. Pass nullptr or empty to disable.
    void SetPath(const wchar_t* path) {
        std::lock_guard<std::mutex> lock(mu_);
        CloseLocked();
        path_ = path ? path : L"";
        if (!path_.empty()) {
            OpenLocked();
        }
    }

    // Append a single line (without newline).
    void Write(const char* line) {
        if (!line) return;
        std::lock_guard<std::mutex> lock(mu_);
        if (!fp_) return;
        std::fputs(line, fp_);
        std::fputc('\n', fp_);
        std::fflush(fp_);
        bytes_ += std::strlen(line) + 1;
        if (bytes_ >= kMaxBytesPerFile) {
            RotateLocked();
        }
    }

    ~LogFile() {
        std::lock_guard<std::mutex> lock(mu_);
        CloseLocked();
    }

private:
    LogFile() = default;

    void OpenLocked() {
        std::string narrow(path_.begin(), path_.end());
        fp_ = std::fopen(narrow.c_str(), "ab");
        if (fp_) {
            std::fseek(fp_, 0, SEEK_END);
            bytes_ = static_cast<std::size_t>(std::ftell(fp_));
        }
    }

    void CloseLocked() {
        if (fp_) { std::fclose(fp_); fp_ = nullptr; }
        bytes_ = 0;
    }

    void RotateLocked() {
        CloseLocked();
        // Shift older generations out of the way.
        for (int i = kMaxFiles - 1; i >= 1; --i) {
            MoveLocked(MakePath(i - 1), MakePath(i));
        }
        // Reopen so the next write goes to a fresh file.
        OpenLocked();
    }

    std::string MakePath(int generation) const {
        std::string narrow(path_.begin(), path_.end());
        if (generation == 0) return narrow;
        return narrow + "." + std::to_string(generation);
    }

    void MoveLocked(const std::string& from, const std::string& to) {
        std::remove(to.c_str());
        std::rename(from.c_str(), to.c_str());
    }

    std::mutex     mu_;
    std::wstring   path_;
    std::FILE*     fp_       = nullptr;
    std::size_t    bytes_    = 0;
};

}  // namespace omnirender
