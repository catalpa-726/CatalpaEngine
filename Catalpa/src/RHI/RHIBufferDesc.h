#pragma once

// 定义 Buffer 容量、用途和内存访问意图
namespace Catalpa {

    enum class RHIBufferUsage : uint32_t {
        None = 0,
        TransferSource = 1u << 0,
        TransferDestination = 1u << 1,
        Vertex = 1u << 2,
        Index = 1u << 3
    };

    constexpr RHIBufferUsage operator|(RHIBufferUsage left, RHIBufferUsage right) noexcept
    {
        return static_cast<RHIBufferUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    [[nodiscard]] constexpr bool HasBufferUsage(RHIBufferUsage usage, RHIBufferUsage required) noexcept
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
    }

    // Device 表示优先供 GPU 使用；Upload 表示允许 CPU 顺序写入。
    enum class RHIMemoryDomain { Device, Upload };

    struct RHIBufferDesc {
        uint64_t size = 0; // 字节。
        RHIBufferUsage usage = RHIBufferUsage::None;
        RHIMemoryDomain memory = RHIMemoryDomain::Device;
    };

}