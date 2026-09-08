// filepath: core/resources/BufferDesc.h
#pragma once

#include <cstdint>

namespace omnirender::core {

enum class BufferUsage : uint16_t {
    None           = 0,
    ConstantBuffer = 1 << 0,
    VertexBuffer   = 1 << 1,
    IndexBuffer    = 1 << 2,
    Structured     = 1 << 3,
    ByteAddress    = 1 << 4,
    UnorderedAccess= 1 << 5
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] constexpr bool HasFlag(BufferUsage mask, BufferUsage flag) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(flag)) != 0;
}

struct BufferDesc {
    uint32_t    byte_width   = 0;
    uint32_t    stride_bytes = 0;
    BufferUsage usage        = BufferUsage::ConstantBuffer;
    const char* debug_name   = nullptr;
};

}  // namespace omnirender::core
