#pragma once

#include "VulkanCommandPool.h"
#include "VulkanFence.h"
#include "VulkanBinarySemaphore.h"
#include "VulkanCommandList.h"

// 集中持有一个帧槽的命令资源、提交 Fence 和 Acquire 信号量
namespace Catalpa {

    class VulkanDevice;

    class VulkanFrameContext {
    public:
        explicit VulkanFrameContext(const VulkanDevice& device);
        ~VulkanFrameContext() = default;

        VulkanFrameContext(const VulkanFrameContext&) = delete;
        VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;

        [[nodiscard]] RHIStatus Init(uint32_t queueFamily);
        [[nodiscard]] RHIStatus PrepareReuse(uint64_t timeoutNanoseconds, bool& ready);

        VkCommandBuffer GetCommandBuffer() const { return m_CommandList.GetHandle(); }
        VulkanCommandList& GetCommandList() { return m_CommandList; }
        VulkanFence& GetFence() { return m_Fence; }
        VulkanBinarySemaphore& GetAcquireSemaphore() { return m_AcquireSemaphore; }

    private:
        VulkanFence m_Fence;
        VulkanBinarySemaphore m_AcquireSemaphore;
        VulkanCommandPool m_CommandPool;
        VulkanCommandList m_CommandList;
    };

}