#include "catpch.h"
#include "VulkanCommandList.h"
#include "VulkanCommandPool.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus VulkanCommandList::Init(VulkanCommandPool& pool)
    {
        CAT_CORE_ASSERT(m_CommandBuffer == VK_NULL_HANDLE, "Vulkan command list already initialized!");

        const RHIStatus status = pool.AllocatePrimary(m_CommandBuffer);
        if (status.IsSuccess()) m_State = RHICommandListState::Initial;
        return status;
    }

    RHIStatus VulkanCommandList::Begin()
    {
        if (m_State != RHICommandListState::Initial) {
            CAT_CORE_ASSERT(false, "Command list must be reset before recording!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::Begin" };
        }

        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };

        const RHIStatus status = MakeVulkanStatus(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo), "vkBeginCommandBuffer");
        m_State = status.IsSuccess() ? RHICommandListState::Recording : RHICommandListState::Invalid;
        return status;
    }

    RHIStatus VulkanCommandList::End()
    {
        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "End the rendering scope before ending the command list!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::End" };
        }

        const RHIStatus status = MakeVulkanStatus(vkEndCommandBuffer(m_CommandBuffer), "vkEndCommandBuffer");
        m_State = status.IsSuccess() ? RHICommandListState::Executable : RHICommandListState::Invalid;
        return status;
    }

    // 	记录已成功接受的提交，本身不执行提交
    void VulkanCommandList::MarkSubmitted()
    {
        CAT_CORE_ASSERT(m_State == RHICommandListState::Executable, "Command list must be executable before submission!");
        m_State = RHICommandListState::Pending;
    }

    // 录制已经给定的依赖，不自行推导资源状态
    RHIStatus VulkanCommandList::PipelineBarrier(const VkDependencyInfo& dependencyInfo)
    {
        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "Pipeline barriers must be recorded outside a rendering scope!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::PipelineBarrier" };
        }

        vkCmdPipelineBarrier2(m_CommandBuffer, &dependencyInfo);
        return {};
    }

    // 开始渲染段，附件、清除值和渲染区域由 renderingInfo 提供
    RHIStatus VulkanCommandList::BeginRendering(const VkRenderingInfo& renderingInfo)
    {
        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "Command list must be recording outside a rendering scope!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::BeginRendering" };
        }

        if (renderingInfo.flags != 0 || renderingInfo.viewMask != 0)
            return { .code = RHIErrorCode::Unsupported, .operation = "VulkanCommandList::BeginRendering" };

        vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
        m_State = RHICommandListState::Rendering;
        return {};
    }

    // 结束渲染段，恢复为普通录制阶段
    RHIStatus VulkanCommandList::EndRendering()
    {
        if (m_State != RHICommandListState::Rendering) {
            CAT_CORE_ASSERT(false, "No rendering scope is active!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::EndRendering" };
        }

        vkCmdEndRendering(m_CommandBuffer);
        m_State = RHICommandListState::Recording;
        return {};
    }

    // 录制动态视口和裁剪区域，允许在普通录制阶段或渲染段内调用
    RHIStatus VulkanCommandList::SetViewportAndScissor(const VkViewport& viewport, const VkRect2D& scissor)
    {
        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering) {
            CAT_CORE_ASSERT(false, "Command list must be recording before setting viewport and scissor!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::SetViewportAndScissor" };
        }

        vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
        return {};
    }

}