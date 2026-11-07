#pragma once
#include "RHI/RHIDevice.h"
namespace Catalpa {
    // 帧槽owner；旧句柄仍由后端命令引用保活，CPU只在对应槽完成后重写Upload。
    struct ComputeBuffer {
        RHIDevice* device = nullptr;
        BufferHandle buffer{};
        uint64_t bytes = 0;
        ComputeBuffer() = default;
        ComputeBuffer(const ComputeBuffer&) = delete;
        ComputeBuffer& operator=(const ComputeBuffer&) = delete;
        ComputeBuffer(ComputeBuffer&& other) noexcept {
            *this = std::move(other);
        }
        ComputeBuffer& operator=(ComputeBuffer&& other) noexcept {
            if (this != &other) {
                Reset();
                device = other.device;
                buffer = std::exchange(other.buffer, {});
                bytes = std::exchange(other.bytes, 0);
            }
            return *this;
        }
        ~ComputeBuffer() {
            Reset();
        }
        void Reset() noexcept {
            if (device)
                device->ReleaseBuffer(std::exchange(buffer, {}));
            bytes = 0;
        }
        RHIStatus Ensure(RHIDevice& owner, uint64_t size, RHIBufferUsage usage, RHIMemoryDomain memory) {
            if (!size || size > owner.GetCapabilities().maxStorageBufferRange)
                return {RHIErrorCode::Unsupported, 0, "Compute/buffer bound"};
            if (buffer.HasValue() && bytes == size)
                return {};
            BufferHandle replacement;
            if (const auto status = owner.CreateBuffer({size, usage, memory}, replacement);
                !status.IsSuccess())
                return status;
            Reset();
            device = &owner;
            buffer = replacement;
            bytes = size;
            return {};
        }
    };
} // namespace Catalpa
