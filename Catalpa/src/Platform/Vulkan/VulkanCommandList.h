#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandList.h"

// 实现 Vulkan 命令录制，借用命令池分配的缓冲
namespace Catalpa {

    class VulkanCommandPool;
    class VulkanFrameContext;

    class VulkanCommandList : public RHICommandList {
    public:
        VulkanCommandList() = default;
        ~VulkanCommandList() override = default;

        VulkanCommandList(const VulkanCommandList&) = delete;
        VulkanCommandList& operator=(const VulkanCommandList&) = delete;

        [[nodiscard]] RHIStatus Init(VulkanCommandPool& pool);
        [[nodiscard]] RHIStatus Begin() override;
        [[nodiscard]] RHIStatus End() override;

        RHICommandListState GetState() const override { return m_State; }
        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }

        // 仅在实际提交成功后调用。
        void MarkSubmitted();

    private:
        // 	在原生命令池重置成功后同步恢复 Initial
        void OnPoolReset() { m_State = RHICommandListState::Initial; }

        void Invalidate() { m_State = RHICommandListState::Invalid; }

    private:
        friend class VulkanFrameContext;
        friend class VulkanQueue;

        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        RHICommandListState m_State = RHICommandListState::Uninitialized;
    };

}