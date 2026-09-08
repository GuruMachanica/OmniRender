// filepath: core/capability/Platform.h
#pragma once

#include <cstdint>
#include <string_view>

namespace omnirender::core {

enum class Platform : uint8_t {
    Unknown = 0,
    Windows,
    Linux,
    MacOS
};

[[nodiscard]] constexpr std::string_view PlatformToString(Platform p) noexcept {
    switch (p) {
        case Platform::Windows: return "Windows";
        case Platform::Linux:   return "Linux";
        case Platform::MacOS:   return "macOS";
        default:                return "Unknown";
    }
}

}  // namespace omnirender::core
