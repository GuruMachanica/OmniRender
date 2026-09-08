// filepath: core/resources/ResourceHandle.h
#pragma once

#include <cstdint>

namespace omnirender::core {

struct ResourceHandle {
    uint32_t id         = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] constexpr bool operator==(const ResourceHandle& other) const noexcept {
        return id == other.id && generation == other.generation;
    }

    [[nodiscard]] constexpr bool operator!=(const ResourceHandle& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace omnirender::core
