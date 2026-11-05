#pragma once

// 定义 Buffer 容量、用途和内存访问意图
namespace Catalpa {

    enum class RHIBufferUsage : uint32_t {
        None = 0,
        TransferSource = 1u << 0,
        TransferDestination = 1u << 1,
        Vertex = 1u << 2,
        Index = 1u << 3,
        Uniform = 1u << 4
    };

    constexpr RHIBufferUsage operator|(RHIBufferUsage left, RHIBufferUsage right) noexcept
    {
        return static_cast<RHIBufferUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    [[nodiscard]] constexpr bool HasBufferUsage(RHIBufferUsage usage, RHIBufferUsage required) noexcept
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
    }

    // Readback 由 GPU 写、CPU 在完成点之后读取，不与 Upload 的顺序写缓存策略混用。
    enum class RHIMemoryDomain { Device, Upload, Readback };

    struct RHIBufferDesc {
        uint64_t size = 0; // 字节。
        RHIBufferUsage usage = RHIBufferUsage::None;
        RHIMemoryDomain memory = RHIMemoryDomain::Device;
    };

}
