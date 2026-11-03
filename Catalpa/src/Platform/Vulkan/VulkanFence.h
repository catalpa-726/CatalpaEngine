#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"

// 声明 Fence 的所有权、队列操作状态和 CPU 等待接口。原生句柄只供 Vulkan 后端使用。
namespace Catalpa {

    class VulkanDevice;

    enum class VulkanFenceState {
        Unsubmitted,        // 未置位，尚未登记关联队列操作
        Pending,            // 相关队列操作已入队，尚未观察到完成
        Signaled            // 已置位；可能来自创建参数，也可能来自关联队列操作完成
    };

    class VulkanFence {
    public:
        explicit VulkanFence(const VulkanDevice& device);
        ~VulkanFence();

        VulkanFence(const VulkanFence&) = delete;
        VulkanFence& operator=(const VulkanFence&) = delete;

        [[nodiscard]] RHIStatus Init(bool signaled = false);
        [[nodiscard]] RHIStatus Wait(uint64_t timeoutNanoseconds, bool& signaled);
        [[nodiscard]] RHIStatus Reset();

        void MarkSubmitted();

        // 调用前，拥有者必须结束所有引用此 Fence 的操作。
        void Shutdown();

        VkFence GetHandle() const { return m_Fence; }
        VulkanFenceState GetState() const { return m_State; }

    private:
        const VulkanDevice& m_Device;
        VkFence m_Fence = VK_NULL_HANDLE;
        VulkanFenceState m_State = VulkanFenceState::Unsubmitted;
    };

}