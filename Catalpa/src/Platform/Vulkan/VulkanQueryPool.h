#pragma once

#include "RHI/RHIQueryDesc.h"
#include "VulkanHeaders.h"

namespace Catalpa {
    class VulkanDevice;

    // 唯一原生池 owner；命令列表通过共享寿命引用保留到帧槽安全重置。
    class VulkanQueryPool final : public RHIQueryPool {
    public:
        explicit VulkanQueryPool(VulkanDevice& device) : m_Device(device) {}
        ~VulkanQueryPool() override;
        [[nodiscard]] RHIStatus Init(const RHIQueryDesc& desc);
        DeviceId GetDeviceId() const noexcept override;
        uint32_t GetCount() const noexcept override { return m_Count; }
        uint32_t GetTimestampValidBits() const noexcept override { return m_ValidBits; }
        float GetTimestampPeriodNanoseconds() const noexcept override { return m_Period; }
        [[nodiscard]] RHIStatus GetResults(uint32_t first, std::span<RHIQueryResult> results) override;
        VkQueryPool GetHandle() const noexcept { return m_Handle; }

    private:
        VulkanDevice& m_Device;
        VkQueryPool m_Handle = VK_NULL_HANDLE;
        uint32_t m_Count = 0;
        uint32_t m_ValidBits = 0;
        float m_Period = 0.0f;
    };
}
