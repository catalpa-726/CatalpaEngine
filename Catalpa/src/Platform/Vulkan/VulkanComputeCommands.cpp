#include "catpch.h"
#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanResourceRegistry.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHIResourceState.h"

namespace Catalpa {
    RHIStatus VulkanCommandList::BindComputePipeline(ComputePipelineHandle pipeline) {
        constexpr const char* operation = "BindComputePipeline";
        if (m_State != RHICommandListState::Recording)
            return {RHIErrorCode::InvalidState, 0, operation};
        auto& registry = m_Device.GetResourceRegistry();
        const auto* record = registry.ResolveGraphicsPipeline(pipeline);
        if (!record || !record->compute)
            return {RHIErrorCode::InvalidDescription, 0, operation};
        if (std::ranges::find(m_RetainedPipelines, pipeline) == m_RetainedPipelines.end()) {
            m_RetainedPipelines.push_back(pipeline);
            if (const auto status = registry.RetainPipeline(pipeline); !status.IsSuccess()) {
                m_RetainedPipelines.pop_back();
                return status;
            }
        }
        std::ranges::copy(record->bindings, m_BoundBindings.begin());
        m_BoundBindingCount = record->bindings.size();
        m_BoundBindingLayout = record->bindingLayout;
        m_BoundPipelineLayout = record->layout;
        m_PushConstantBytes = record->pushConstantBytes;
        m_PushConstantStages = record->pushConstantStages;
        m_PushConstantsSet = m_PushConstantBytes == 0;
        m_GraphicsBindingsSet = m_BoundBindingCount == 0;
        m_BoundCompute = true;
        m_StorageBindings.clear();
        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, record->pipeline);
        return {};
    }

    RHIStatus VulkanCommandList::SetComputeBindings(std::span<const RHIBinding> bindings) {
        if (!m_BoundCompute || m_State != RHICommandListState::Recording)
            return {RHIErrorCode::InvalidState, 0, "SetComputeBindings"};
        return SetBindings(bindings);
    }

    RHIStatus VulkanCommandList::SetComputePushConstants(std::span<const std::byte> bytes) {
        m_PushConstantsSet = false;
        if (!m_BoundCompute || m_State != RHICommandListState::Recording ||
            m_BoundPipelineLayout == VK_NULL_HANDLE)
            return {RHIErrorCode::InvalidState, 0, "SetComputePushConstants"};
        if (bytes.size() != m_PushConstantBytes || bytes.empty())
            return {RHIErrorCode::InvalidDescription, 0, "SetComputePushConstants"};
        vkCmdPushConstants(m_CommandBuffer, m_BoundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(bytes.size()), bytes.data());
        m_PushConstantsSet = true;
        return {};
    }

    RHIStatus VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
        constexpr const char* operation = "Dispatch";
        const auto& caps = m_Device.GetCapabilities();
        if (m_State != RHICommandListState::Recording || !m_BoundCompute || !m_GraphicsBindingsSet ||
            !m_PushConstantsSet)
            return {RHIErrorCode::InvalidState, 0, operation};
        if (!x || !y || !z || x > caps.maxComputeWorkGroupCount[0] || y > caps.maxComputeWorkGroupCount[1] ||
            z > caps.maxComputeWorkGroupCount[2])
            return {RHIErrorCode::InvalidDescription, 0, operation};

        // 所有可能分配均早于原生命令；写范围由同源RGComputeDesc的完整输出契约给出。
        for (const auto& binding : m_StorageBindings) {
            LocalBufferState* local = nullptr;
            if (const auto status = GetLocalBufferState(binding.buffer, local); !status.IsSuccess())
                return status;
            if (binding.initializesRange)
                local->snapshot.state.initialized.reserve(local->snapshot.state.initialized.size() +
                                                          m_StorageBindings.size());
        }
        vkCmdDispatch(m_CommandBuffer, x, y, z);
        for (const auto& binding : m_StorageBindings)
            if (binding.initializesRange) {
                LocalBufferState* local = nullptr;
                if (const auto status = GetLocalBufferState(binding.buffer, local); !status.IsSuccess()) {
                    Invalidate();
                    return status;
                }
                MarkInitialized(local->snapshot.state.initialized, binding.offset, binding.size);
            }
        return {};
    }

    RHIStatus VulkanCommandList::DrawIndexedIndirect(BufferHandle buffer, uint64_t offset) {
        constexpr const char* operation = "DrawIndexedIndirect";
        if (m_State != RHICommandListState::Rendering || m_BoundCompute || !m_GraphicsBindingsSet ||
            !m_PushConstantsSet || !m_IndexBufferBound || !m_VertexBufferBound || !m_ViewportAndScissorSet ||
            m_BoundColorFormat != m_RenderingColorFormat || m_BoundDepthFormat != m_RenderingDepthFormat ||
            (m_RenderingDepthReadOnly && m_BoundDepthWrite))
            return {RHIErrorCode::InvalidState, 0, operation};
        const auto* record = m_Device.GetResourceRegistry().ResolveBuffer(buffer);
        LocalBufferState* local = nullptr;
        if (!record || offset % 4 != 0 || !HasBufferUsage(record->desc.usage, RHIBufferUsage::Indirect) ||
            !IsValidBufferRange(record->desc.size, offset, sizeof(VkDrawIndexedIndirectCommand)))
            return {RHIErrorCode::InvalidDescription, 0, operation};
        if (const auto status = GetLocalBufferState(buffer, local); !status.IsSuccess())
            return status;
        if (local->snapshot.state.access.access != RHIResourceAccess::IndirectRead ||
            !RangeContains(local->snapshot.state.initialized, offset, sizeof(VkDrawIndexedIndirectCommand)))
            return {RHIErrorCode::InvalidDescription, 0, operation};
        if (const auto status = RetainBuffer(buffer); !status.IsSuccess())
            return status;
        // 每条命令单独录制，不依赖multiDrawIndirect或drawIndirectCount特性。
        vkCmdDrawIndexedIndirect(m_CommandBuffer, record->buffer, offset, 1,
                                 sizeof(VkDrawIndexedIndirectCommand));
        return {};
    }
} // namespace Catalpa
