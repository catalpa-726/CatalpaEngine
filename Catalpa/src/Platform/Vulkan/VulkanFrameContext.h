#pragma once

#include "VulkanCommandPool.h"
#include "VulkanFence.h"
#include "VulkanBinarySemaphore.h"
#include "VulkanCommandList.h"
#include "RHI/RHISwapchainDesc.h"

// 集中持有一个帧槽的命令资源、提交 Fence 和 Acquire 信号量
namespace Catalpa {

    class VulkanDevice;
    class VulkanQueue;
    class VulkanSwapchain;

    class VulkanFrameContext {
    public:
        explicit VulkanFrameContext(VulkanDevice& device);
        ~VulkanFrameContext();

        VulkanFrameContext(const VulkanFrameContext&) = delete;
        VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;

        [[nodiscard]] RHIStatus Init(uint32_t queueFamily);
        [[nodiscard]] RHIStatus PrepareReuse(uint64_t timeoutNanoseconds, bool& ready);

        VkCommandBuffer GetCommandBuffer() const { return m_CommandList.GetHandle(); }
        VulkanCommandList& GetCommandList() { return m_CommandList; }

        VulkanFence& GetFence() { return m_Fence; }
        VulkanBinarySemaphore& GetAcquireSemaphore() { return m_AcquireSemaphore; }

        // Acquire()：使用本帧的 Acquire 信号量获取图像，并记录尚未提交的获取操作。
        [[nodiscard]] RHIStatus Acquire(VulkanSwapchain& swapchain, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);

        // Submit()：自动连接本帧的命令、Fence、Acquire 等待和呈现信号量。`presentSemaphore` 必须对应刚取得的图像，并来自同一设备。
        [[nodiscard]] RHIStatus Submit(VulkanQueue& queue, VulkanBinarySemaphore& presentSemaphore);

        // 仅取消尚未成功提交的获取；swapchain 和 acquired 必须对应本帧的同一次 Acquire。
        // 调用前不得重建或销毁该交换链；成功后仍需等待 Fence 完成。
        [[nodiscard]] RHIStatus CancelAcquire(VulkanQueue& queue, VulkanSwapchain& swapchain, RHIAcquireResult& acquired);

    private:
        // 三个阶段分别表示：允许获取图像、取得图像但尚未提交等待、提交已接受但尚未完成帧槽复用准备。
        enum class State { Available, Acquired, Submitted };
        State m_State = State::Available;

        VulkanFence m_Fence;
        VulkanBinarySemaphore m_AcquireSemaphore;

        VulkanCommandPool m_CommandPool;
        VulkanCommandList m_CommandList;
    };

}