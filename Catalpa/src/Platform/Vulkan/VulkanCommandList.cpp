#include "catpch.h"
#include "VulkanCommandList.h"
#include "VulkanCommandPool.h"
#include "VulkanUtils.h"
#include "VulkanDevice.h"
#include "VulkanResourceRegistry.h"
#include "VulkanConversions.h"
#include "VulkanBarrierCompiler.h"

namespace Catalpa {

    VulkanCommandList::~VulkanCommandList() { ReleaseTextureViews(); ReleasePipelines(); }

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
        return m_Device.ObserveStatus(status);
    }

    RHIStatus VulkanCommandList::End()
    {
        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "End the rendering scope before ending the command list!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::End" };
        }

        const RHIStatus status = MakeVulkanStatus(vkEndCommandBuffer(m_CommandBuffer), "vkEndCommandBuffer");
        m_State = status.IsSuccess() ? RHICommandListState::Executable : RHICommandListState::Invalid;
        return m_Device.ObserveStatus(status);
    }

    // 	记录已成功接受的提交，本身不执行提交
    void VulkanCommandList::MarkSubmitted()
    {
        CAT_CORE_ASSERT(m_State == RHICommandListState::Executable, "Command list must be executable before submission!");
        m_State = RHICommandListState::Pending;
    }

    // 录制已经给定的依赖，不自行推导资源状态
    RHIStatus VulkanCommandList::PipelineBarrier(std::span<const RHITextureBarrier> barriers)
    {
        constexpr const char* operation = "VulkanCommandList::PipelineBarrier";

        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "Pipeline barriers must be recorded outside a rendering scope!");
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        }

        if (barriers.empty()) return {};

        if (barriers.size() > std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto& registry = m_Device.GetResourceRegistry();
        std::vector<VkImageMemoryBarrier2> nativeBarriers;
        nativeBarriers.reserve(barriers.size());

        for (const auto& desc : barriers) {
            VkImageMemoryBarrier2 native{};
            if (const RHIStatus status = CompileVulkanTextureBarrier(desc, registry, native); !status.IsSuccess()) return status;

            if (std::ranges::any_of(nativeBarriers, [image = native.image](const auto& existing) { return existing.image == image; }))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            nativeBarriers.push_back(native);
        }

        const size_t first = m_RetainedViews.size();
        m_RetainedViews.reserve(first + barriers.size());

        for (const auto& desc : barriers) {
            if (const RHIStatus status = RetainTextureView(desc.view); !status.IsSuccess()) {
                ReleaseTextureViews(first);
                return status;
            }
        }

        const VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = static_cast<uint32_t>(nativeBarriers.size()),
            .pImageMemoryBarriers = nativeBarriers.data()
        };

        vkCmdPipelineBarrier2(m_CommandBuffer, &dependencyInfo);
        return {};
    }

    // 开始渲染段，附件、清除值和渲染区域由 renderingInfo 提供
    RHIStatus VulkanCommandList::BeginRendering(const RHIRenderingDesc& desc)
    {
        constexpr const char* operation = "VulkanCommandList::BeginRendering";

        if (m_State != RHICommandListState::Recording) {
            CAT_CORE_ASSERT(false, "Command list must be recording outside a rendering scope!");
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        }

        if (desc.sampleCount != 1)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const auto& capabilities = m_Device.GetCapabilities();
        const auto& area = desc.renderArea;

        if (desc.colorAttachments.empty() || desc.colorAttachments.size() > capabilities.maxColorAttachments || area.IsEmpty() || area.offset.x < 0 || area.offset.y < 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const uint64_t right = static_cast<uint64_t>(area.offset.x) + area.extent.width;
        const uint64_t bottom = static_cast<uint64_t>(area.offset.y) + area.extent.height;

        if (right > capabilities.maxRenderTargetExtent.width || bottom > capabilities.maxRenderTargetExtent.height)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        auto& registry = m_Device.GetResourceRegistry();
        std::vector<VkRenderingAttachmentInfo> attachments;
        attachments.reserve(desc.colorAttachments.size());

        PixelFormat colorFormat = PixelFormat::Unknown;

        for (const auto& attachment : desc.colorAttachments) {
            const auto* record = registry.ResolveTextureView(attachment.view);

            if (!record || right > record->extent.width || bottom > record->extent.height)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            if (record->sampleCount != 1 || ToVulkanFormat(record->format) == VK_FORMAT_UNDEFINED)
                return { .code = RHIErrorCode::Unsupported, .operation = operation };

            colorFormat = record->format;

            const VkAttachmentLoadOp loadOp = ToVulkanLoadOp(attachment.loadOp);
            const VkAttachmentStoreOp storeOp = ToVulkanStoreOp(attachment.storeOp);

            if (loadOp == VK_ATTACHMENT_LOAD_OP_MAX_ENUM || storeOp == VK_ATTACHMENT_STORE_OP_MAX_ENUM)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            const auto& color = attachment.clearColor;

            attachments.push_back({
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = record->view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = {.color = {.float32 = { color[0], color[1], color[2], color[3] } } }
                });
        }

        const size_t first = m_RetainedViews.size();
        m_RetainedViews.reserve(first + desc.colorAttachments.size());

        for (const auto& attachment : desc.colorAttachments) {
            if (const RHIStatus status = RetainTextureView(attachment.view); !status.IsSuccess()) {
                ReleaseTextureViews(first);
                return status;
            }
        }

        const VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { area.offset.x, area.offset.y },
                .extent = { area.extent.width, area.extent.height }
            },
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
            .pColorAttachments = attachments.data()
        };

        vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
        m_RenderingColorFormat = desc.colorAttachments.size() == 1 ? colorFormat : PixelFormat::Unknown;
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
    RHIStatus VulkanCommandList::SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor)
    {
        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering) {
            CAT_CORE_ASSERT(false, "Command list must be recording before setting viewport and scissor!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::SetViewportAndScissor" };
        }

        const VkViewport nativeViewport{
            .x = viewport.x,
            .y = viewport.y,
            .width = viewport.width,
            .height = viewport.height,
            .minDepth = viewport.minDepth,
            .maxDepth = viewport.maxDepth
        };

        const VkRect2D nativeScissor{
            .offset = {.x = scissor.offset.x, .y = scissor.offset.y },
            .extent = {.width = scissor.extent.width, .height = scissor.extent.height }
        };

        vkCmdSetViewport(m_CommandBuffer, 0, 1, &nativeViewport);
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &nativeScissor);
        m_ViewportAndScissorSet = true;
        return {};
    }

    RHIStatus VulkanCommandList::RetainTextureView(TextureViewHandle handle)
    {
        if (std::ranges::find(m_RetainedViews, handle) != m_RetainedViews.end()) return {};

        m_RetainedViews.push_back(handle);

        const RHIStatus status = m_Device.GetResourceRegistry().RetainTextureView(handle);
        if (!status.IsSuccess()) m_RetainedViews.pop_back();
        return status;
    }

    void VulkanCommandList::ReleaseTextureViews(size_t first) noexcept
    {
        while (m_RetainedViews.size() > first) {
            m_Device.GetResourceRegistry().ReleaseTextureView(m_RetainedViews.back());
            m_RetainedViews.pop_back();
        }
    }

    void VulkanCommandList::OnPoolReset()
    {
        ReleaseTextureViews();
        ReleasePipelines();

        m_BoundColorFormat = PixelFormat::Unknown;
        m_RenderingColorFormat = PixelFormat::Unknown;
        m_ViewportAndScissorSet = false;
        m_State = RHICommandListState::Initial;
    }

    RHIStatus VulkanCommandList::BindGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::BindGraphicsPipeline" };

        auto& registry = m_Device.GetResourceRegistry();
        const auto* record = registry.ResolveGraphicsPipeline(pipeline);

        if (!record)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanCommandList::BindGraphicsPipeline" };

        if (std::ranges::find(m_RetainedPipelines, pipeline) == m_RetainedPipelines.end()) {
            m_RetainedPipelines.push_back(pipeline);

            if (const RHIStatus status = registry.RetainPipeline(pipeline); !status.IsSuccess()) {
                m_RetainedPipelines.pop_back();
                return status;
            }
        }

        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, record->pipeline);
        m_BoundColorFormat = record->colorFormat;
        return {};
    }

    RHIStatus VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (m_State != RHICommandListState::Rendering || m_BoundColorFormat == PixelFormat::Unknown || m_BoundColorFormat != m_RenderingColorFormat || !m_ViewportAndScissorSet)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::Draw" };

        vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        return {};
    }

    void VulkanCommandList::ReleasePipelines() noexcept
    {
        while (!m_RetainedPipelines.empty()) {
            m_Device.GetResourceRegistry().ReleasePipeline(m_RetainedPipelines.back());
            m_RetainedPipelines.pop_back();
        }
    }

}