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
        // 构造与所有权
        VulkanFrameContext(VulkanDevice& device, uint32_t slot);
        ~VulkanFrameContext();

        VulkanFrameContext(const VulkanFrameContext&) = delete;
        VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;

        // 初始化与帧槽复用，等待时间以纳秒为单位。
        [[nodiscard]] RHIStatus Init(uint32_t queueFamily);
        [[nodiscard]] RHIStatus PrepareReuse(uint64_t timeoutNanoseconds, bool& ready);

        // 查询当前帧身份，以及是否仍持有尚未收尾的获取。
        RHIFrameToken GetToken() const;
        bool HasActiveAcquisition() const { return m_ActiveSwapchain != nullptr; }

        // 借用帧内命令和同步对象
        VkCommandBuffer GetCommandBuffer() const { return m_CommandList.GetHandle(); }
        VulkanCommandList& GetCommandList() { return m_CommandList; }
        VulkanFence& GetFence() { return m_Fence; }
        VulkanBinarySemaphore& GetAcquireSemaphore() { return m_AcquireSemaphore; }

        // 取得图像与图形提交
        [[nodiscard]] RHIStatus Acquire(VulkanSwapchain& swapchain, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired);
        [[nodiscard]] RHIStatus Submit(VulkanQueue& queue, const RHIGpuDependency& wait, const RHIGpuDependency& signal);

        // 检查提交与获取对应关系，再进入原生呈现。
        [[nodiscard]] RHIStatus Present(VulkanSwapchain& swapchain, const RHIAcquireResult& acquired, RHIPresentOutcome& outcome);

        // 提交等待以消耗未提交帧的 Acquire 信号量，图像留待交换链重建时释放。
        [[nodiscard]] RHIStatus CancelAcquire(VulkanQueue& queue, VulkanSwapchain& swapchain, RHIAcquireResult& acquired);

    private:
        // 帧状态与身份
        enum class State { Available, Acquired, Submitted };

        VulkanDevice& m_Device;
        uint32_t m_Slot;
        uint64_t m_Generation = 0;
        State m_State = State::Available;

        // 尚未交给呈现或取消提交的获取
        VulkanSwapchain* m_ActiveSwapchain = nullptr;
        RHIAcquireResult m_Acquired{};

        // 析构体先销毁原生命令池，随后命令列表成员归还资源引用。
        VulkanFence m_Fence;
        VulkanBinarySemaphore m_AcquireSemaphore;
        VulkanCommandPool m_CommandPool;
        VulkanCommandList m_CommandList;
    };

}