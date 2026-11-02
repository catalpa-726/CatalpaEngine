#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

// 管理属于指定队列族的命令池，提供主命令缓冲分配和整池重置
namespace Catalpa {

    class VulkanDevice;

    class VulkanCommandPool {
    public:
        explicit VulkanCommandPool(const VulkanDevice& device);
        ~VulkanCommandPool();

        VulkanCommandPool(const VulkanCommandPool&) = delete;
        VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;

        [[nodiscard]] RHIStatus Init(uint32_t queueFamily);
        [[nodiscard]] RHIStatus AllocatePrimary(VkCommandBuffer& commandBuffer);

        // 调用前，池内命令缓冲的录制和 GPU 使用必须已经结束。
        [[nodiscard]] RHIStatus Reset();
        void Shutdown();

        VkCommandPool GetHandle() const { return m_Pool; }
        uint32_t GetQueueFamily() const { return m_QueueFamily; }

    private:
        const VulkanDevice& m_Device;
        VkCommandPool m_Pool = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = VK_QUEUE_FAMILY_IGNORED;
    };

}