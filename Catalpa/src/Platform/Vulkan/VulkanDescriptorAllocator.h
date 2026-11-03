#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class VulkanDevice;

    class VulkanDescriptorAllocator {
    public:
        explicit VulkanDescriptorAllocator(const VulkanDevice& device) : m_Device(device) {}
        ~VulkanDescriptorAllocator();

        VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
        VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;

        // layout 必须来自本设备，且满足 RHIMaxBindingsPerSet 及当前支持的描述符类型。
        [[nodiscard]] RHIStatus Allocate(VkDescriptorSetLayout layout, VkDescriptorSet& descriptorSet);

        // 调用前结束 GPU 使用，并重置或丢弃引用这些 set 的命令。
        // 失败时调用方不得将命令列表恢复为可录制状态。
        [[nodiscard]] RHIStatus Reset();

        // 不等待 GPU；调用前满足与 Reset 相同的生命周期要求。
        void Shutdown();

    private:
        static constexpr uint32_t SetsPerPool = 64;

        const VulkanDevice& m_Device;
        std::vector<VkDescriptorPool> m_Pools;
        size_t m_CurrentPool = 0;
    };

}
