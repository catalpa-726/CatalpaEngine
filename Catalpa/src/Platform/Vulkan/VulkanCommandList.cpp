#include "catpch.h"
#include "VulkanCommandList.h"
#include "VulkanBinding.h"
#include "VulkanCommandPool.h"
#include "VulkanUtils.h"
#include "VulkanDevice.h"
#include "VulkanResourceRegistry.h"
#include "VulkanConversions.h"
#include "VulkanBarrierCompiler.h"
#include "RHI/RHIBufferDesc.h"
#include "VulkanDebugUtils.h"
#include "VulkanResourceStateTracker.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

    VulkanCommandList::~VulkanCommandList()
    {
        m_Descriptors.Shutdown();
        ReleaseTextureViews();
        ReleaseSamplers();
        ReleasePipelines();
        ReleaseBuffers();
    }

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
        if (m_State != RHICommandListState::Recording || m_DebugLabelDepth != 0) {
            CAT_CORE_ASSERT(false, "End rendering and debug label scopes before ending the command list!");
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::End" };
        }

        const RHIStatus status = MakeVulkanStatus(vkEndCommandBuffer(m_CommandBuffer), "vkEndCommandBuffer");
        m_State = status.IsSuccess() ? RHICommandListState::Executable : RHICommandListState::Invalid;
        return m_Device.ObserveStatus(status);
    }

    RHIStatus VulkanCommandList::SetResourceStatePlan(
        std::span<const RHIBufferStateUpdate> buffers,
        std::span<const RHITextureStateUpdate> textures)
    {
        if (m_State != RHICommandListState::Recording || m_ResourceStatePlanBound ||
            !m_RetainedBuffers.empty() || !m_RetainedViews.empty() || !m_RetainedPipelines.empty()) {
            Invalidate();
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::SetResourceStatePlan" };
        }

        auto& registry = m_Device.GetResourceRegistry();

        if (const RHIStatus status = VulkanResourceStateTracker::Validate(registry, buffers, textures, false); !status.IsSuccess()) {
            Invalidate();
            return status;
        }

        std::vector<RHIBufferStateUpdate> bufferUpdates(buffers.begin(), buffers.end());
        std::vector<RHITextureStateUpdate> textureUpdates(textures.begin(), textures.end());

        // 声明不证明初始化；从当前权威快照建立独立账本，不能导入after.initialized。
        m_LocalBufferStates.clear();
        m_LocalBufferStates.reserve(bufferUpdates.size());
        for (const auto& update : bufferUpdates) {
            LocalBufferState* local = nullptr;
            if (const auto status = GetLocalBufferState(update.buffer, local); !status.IsSuccess()) {
                Invalidate();
                return status;
            }
        }
        m_RetainedBuffers.reserve(bufferUpdates.size());
        m_RetainedViews.reserve(textureUpdates.size());

        const auto fail = [this](RHIStatus status) {
            // 尚未录制资源命令，可以归还本次设置取得的引用。
            ReleaseTextureViews();
            ReleaseBuffers();
            Invalidate();
            return status;
            };

        for (const auto& update : bufferUpdates)
            if (const RHIStatus status = RetainBuffer(update.buffer); !status.IsSuccess()) return fail(status);

        for (const auto& update : textureUpdates)
            if (const RHIStatus status = RetainTextureView(update.view); !status.IsSuccess()) return fail(status);

        m_BufferStateUpdates.swap(bufferUpdates);
        m_TextureStateUpdates.swap(textureUpdates);
        m_ResourceStatePlanBound = true;
        m_ResourceStatePlanCommitted = false;
        return {};
    }

    RHIStatus VulkanCommandList::ValidateResourceStatePlan() const noexcept
    {
        if (m_State != RHICommandListState::Executable || !m_ResourceStatePlanBound || m_ResourceStatePlanCommitted)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::ValidateResourceStatePlan" };

        // Set 已保留全部声明资源；之后只追加引用或撤销本次追加的后缀。
        if (m_RetainedBuffers.size() != m_BufferStateUpdates.size() ||
            m_RetainedViews.size() != m_TextureStateUpdates.size())
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::UndeclaredResource" };

        // 提交后要发布的初始化范围也必须有实际录制证据；防止虚假after状态倒灌下一帧。
        for (const auto& update : m_BufferStateUpdates) {
            const auto local = std::ranges::find_if(m_LocalBufferStates,
                [&](const auto& entry) { return entry.buffer == update.buffer; });
            if (local == m_LocalBufferStates.end())
                return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::UnprovenInitialization" };
            for (const auto& range : update.after.initialized)
                if (!RangeContains(local->snapshot.state.initialized, range.offset, range.size))
                    return { .code = RHIErrorCode::InvalidState, .operation = "VulkanCommandList::UnprovenInitialization" };
        }
        return VulkanResourceStateTracker::Validate(
            m_Device.GetResourceRegistry(), m_BufferStateUpdates, m_TextureStateUpdates, true);
    }

    void VulkanCommandList::CommitResourceStatePlan() noexcept
    {
        CAT_CORE_ASSERT(
            m_State == RHICommandListState::Executable && m_ResourceStatePlanBound && !m_ResourceStatePlanCommitted,
            "Resource state plan must be valid and uncommitted!");

        VulkanResourceStateTracker::CommitAccepted(
            m_Device.GetResourceRegistry(), m_BufferStateUpdates, m_TextureStateUpdates);

        m_ResourceStatePlanCommitted = true;
    }

    bool VulkanCommandList::ExportsPresent(TextureViewHandle view) const noexcept
    {
        bool exported = false;

        for (const auto& update : m_TextureStateUpdates) {
            if (update.after.access.layout != RHITextureLayout::Present) continue;

            if (exported || update.view != view) return false;
            exported = true;
        }

        return exported;
    }

    // 记录已成功接受的提交，本身不执行提交
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

    // 开始渲染段，附件、清除值和渲染区域由 desc 提供
    RHIStatus VulkanCommandList::BeginRendering(const RHIRenderingDesc& desc)
    {
        constexpr const char* operation = "VulkanCommandList::BeginRendering";
        if (m_State != RHICommandListState::Recording)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        if (desc.sampleCount != 1 || desc.colorAttachments.size() > 1)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        const auto& capabilities = m_Device.GetCapabilities();
        const auto& area = desc.renderArea;
        if ((desc.colorAttachments.empty() && !desc.depthAttachment)
            || desc.colorAttachments.size() > capabilities.maxColorAttachments
            || area.IsEmpty() || area.offset.x < 0 || area.offset.y < 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        const uint64_t right = static_cast<uint64_t>(area.offset.x) + area.extent.width;
        const uint64_t bottom = static_cast<uint64_t>(area.offset.y) + area.extent.height;
        if (right > capabilities.maxRenderTargetExtent.width || bottom > capabilities.maxRenderTargetExtent.height)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        auto& registry = m_Device.GetResourceRegistry();
        std::vector<VkRenderingAttachmentInfo> attachments;
        attachments.reserve(desc.colorAttachments.size());
        PixelFormat colorFormat = PixelFormat::Unknown, depthFormat = PixelFormat::Unknown;
        for (const auto& attachment : desc.colorAttachments) {
            const auto* record = registry.ResolveTextureView(attachment.view);
            if (!record || !HasTextureUsage(record->usage, RHITextureUsage::ColorAttachment)
                || GetPixelFormatInfo(record->format).isDepth || record->sampleCount != 1
                || right > record->extent.width || bottom > record->extent.height)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            const auto loadOp = ToVulkanLoadOp(attachment.loadOp);
            const auto storeOp = ToVulkanStoreOp(attachment.storeOp);
            if (loadOp == VK_ATTACHMENT_LOAD_OP_MAX_ENUM || storeOp == VK_ATTACHMENT_STORE_OP_MAX_ENUM)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (float value : attachment.clearColor)
                if (!std::isfinite(value)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            colorFormat = record->format;
            const auto& c = attachment.clearColor;
            attachments.push_back({
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = record->view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = { .color = { .float32 = { c[0], c[1], c[2], c[3] } } }
            });
        }
        VkRenderingAttachmentInfo depthInfo{};
        if (desc.depthAttachment) {
            const auto& depth = *desc.depthAttachment;
            const auto* record = registry.ResolveTextureView(depth.view);
            if (!record || !HasTextureUsage(record->usage, RHITextureUsage::DepthAttachment)
                || record->format != PixelFormat::D32_Float || record->sampleCount != 1
                || right > record->extent.width || bottom > record->extent.height
                || !std::isfinite(depth.clearDepth) || depth.clearDepth < 0.0f || depth.clearDepth > 1.0f
                || (depth.readOnly && (depth.loadOp != RHILoadOp::Load || depth.storeOp != RHIStoreOp::Store)))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            const auto loadOp = ToVulkanLoadOp(depth.loadOp);
            const auto storeOp = ToVulkanStoreOp(depth.storeOp);
            if (loadOp == VK_ATTACHMENT_LOAD_OP_MAX_ENUM || storeOp == VK_ATTACHMENT_STORE_OP_MAX_ENUM)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            depthFormat = record->format;
            depthInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = record->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = { .depthStencil = { depth.clearDepth, 0 } }
            };
        }
        const size_t first = m_RetainedViews.size();
        m_RetainedViews.reserve(first + desc.colorAttachments.size() + (desc.depthAttachment ? 1 : 0));
        for (const auto& attachment : desc.colorAttachments)
            if (const auto status = RetainTextureView(attachment.view); !status.IsSuccess()) {
                ReleaseTextureViews(first); return status;
            }
        if (desc.depthAttachment)
            if (const auto status = RetainTextureView(desc.depthAttachment->view); !status.IsSuccess()) {
                ReleaseTextureViews(first); return status;
            }
        const VkRenderingInfo info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { { area.offset.x, area.offset.y }, { area.extent.width, area.extent.height } },
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
            .pColorAttachments = attachments.empty() ? nullptr : attachments.data(),
            .pDepthAttachment = desc.depthAttachment ? &depthInfo : nullptr
        };
        vkCmdBeginRendering(m_CommandBuffer, &info);
        m_RenderingColorFormat = colorFormat;
        m_RenderingDepthFormat = depthFormat;
        m_RenderingDepthReadOnly = desc.depthAttachment && desc.depthAttachment->readOnly;
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

    RHIStatus VulkanCommandList::GetLocalBufferState(BufferHandle buffer, LocalBufferState*& state)
    {
        const auto found = std::ranges::find_if(m_LocalBufferStates,
            [&](const auto& entry) { return entry.buffer == buffer; });
        if (found != m_LocalBufferStates.end()) { state = &*found; return {}; }
        LocalBufferState candidate;
        candidate.buffer = buffer;
        if (const auto status = m_Device.GetBufferState(buffer, candidate.snapshot); !status.IsSuccess()) return status;
        m_LocalBufferStates.push_back(std::move(candidate));
        state = &m_LocalBufferStates.back();
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
            m_Device.GetResourceRegistry().ReleaseTextureViewReference(m_RetainedViews.back());
            m_RetainedViews.pop_back();
        }
    }

    RHIStatus VulkanCommandList::RetainSampler(SamplerHandle sampler)
    {
        if (std::ranges::find(m_RetainedSamplers, sampler) != m_RetainedSamplers.end()) return {};

        m_RetainedSamplers.push_back(sampler);
        const RHIStatus status = m_Device.GetResourceRegistry().RetainSampler(sampler);
        if (!status.IsSuccess()) m_RetainedSamplers.pop_back();
        return status;
    }

    void VulkanCommandList::ReleaseSamplers(size_t first) noexcept
    {
        while (m_RetainedSamplers.size() > first) {
            m_Device.GetResourceRegistry().ReleaseSamplerReference(m_RetainedSamplers.back());
            m_RetainedSamplers.pop_back();
        }
    }

    RHIStatus VulkanCommandList::OnPoolReset()
    {
        if (const RHIStatus status = m_Device.ObserveStatus(m_Descriptors.Reset()); !status.IsSuccess()) {
            Invalidate();
            return status;
        }

        ReleaseTextureViews();
        ReleaseSamplers();
        ReleasePipelines();
        ReleaseBuffers();

        m_BufferStateUpdates.clear();
        m_LocalBufferStates.clear();
        m_TextureStateUpdates.clear();
        m_ResourceStatePlanBound = false;
        m_ResourceStatePlanCommitted = false;

        m_BoundPipelineLayout = VK_NULL_HANDLE;
        m_PushConstantBytes = 0;
        m_PushConstantStages = 0;
        m_PushConstantsSet = false;

        m_BoundBindingLayout = VK_NULL_HANDLE;
        m_BoundBindingCount = 0;
        m_GraphicsBindingsSet = false;

        m_BoundColorFormat = PixelFormat::Unknown;
        m_RenderingColorFormat = PixelFormat::Unknown;
        m_BoundDepthFormat = PixelFormat::Unknown;
        m_RenderingDepthFormat = PixelFormat::Unknown;
        m_BoundDepthWrite = false;
        m_RenderingDepthReadOnly = false;
        m_ViewportAndScissorSet = false;
        m_VertexStride = 0;
        m_VertexAttributeEnd = 0;
        m_VertexBufferBytes = 0;
        m_IndexBufferBytes = 0;
        m_IndexSize = 0;
        m_VertexBufferBound = false;
        m_IndexBufferBound = false;
        m_State = RHICommandListState::Initial;
        m_DebugLabelDepth = 0;
        return {};
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

        std::ranges::copy(record->bindings, m_BoundBindings.begin());
        m_BoundBindingCount = record->bindings.size();
        m_BoundBindingLayout = record->bindingLayout;
        m_GraphicsBindingsSet = m_BoundBindingCount == 0;

        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, record->pipeline);

        m_BoundPipelineLayout = record->layout;
        m_PushConstantBytes = record->pushConstantBytes;
        m_PushConstantStages = record->pushConstantStages;

        // RHI 当前要求每次成功绑定后重新提供该次绘制参数。
        m_PushConstantsSet = m_PushConstantBytes == 0;

        m_BoundColorFormat = record->colorFormat;
        m_BoundDepthFormat = record->depthFormat;
        m_BoundDepthWrite = record->depthWrite;
        m_VertexStride = record->vertexStride;
        m_VertexAttributeEnd = record->vertexAttributeEnd;
        return {};
    }

    RHIStatus VulkanCommandList::SetGraphicsPushConstants(std::span<const std::byte> data)
    {
        constexpr const char* operation = "VulkanCommandList::SetGraphicsPushConstants";

        // 设置失败后，不允许下一次 Draw 静默沿用此前的参数。
        if (m_BoundPipelineLayout != VK_NULL_HANDLE) m_PushConstantsSet = false;

        if ((m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering) ||
            m_BoundPipelineLayout == VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (m_PushConstantBytes == 0 || data.size() != m_PushConstantBytes)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        vkCmdPushConstants(m_CommandBuffer, m_BoundPipelineLayout, m_PushConstantStages, 0, m_PushConstantBytes, data.data());

        m_PushConstantsSet = true;
        return {};
    }

    RHIStatus VulkanCommandList::SetGraphicsBindings(std::span<const RHIBinding> bindings)
    {
        constexpr const char* operation = "VulkanCommandList::SetGraphicsBindings";
        m_GraphicsBindingsSet = false;
        if ((m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering)
            || m_BoundPipelineLayout == VK_NULL_HANDLE || m_BoundBindingLayout == VK_NULL_HANDLE)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        if (bindings.empty() || bindings.size() != m_BoundBindingCount)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        const std::span<const RHIBindingLayoutEntry> layout{ m_BoundBindings.data(), m_BoundBindingCount };
        auto& registry = m_Device.GetResourceRegistry();
        std::array<VkDescriptorImageInfo, RHIMaxBindingsPerSet> images{};
        std::array<VkDescriptorBufferInfo, RHIMaxBindingsPerSet> buffers{};
        std::array<VkWriteDescriptorSet, RHIMaxBindingsPerSet> writes{};
        bool requiresLinearFiltering = false, texturesSupportLinearFiltering = true;
        for (size_t i = 0; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            const auto expected = std::ranges::find(layout, binding.binding, &RHIBindingLayoutEntry::binding);
            if (expected == layout.end() || expected->type != binding.type)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (size_t j=0; j<i; ++j)
                if (bindings[j].binding == binding.binding)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            switch (binding.type) {
            case RHIBindingType::SampledTexture: {
                const auto* record = registry.ResolveTextureView(binding.texture);
                if (!record || !HasTextureUsage(record->usage, RHITextureUsage::Sampled)
                    || GetPixelFormatInfo(record->format).isDepth || binding.sampler.HasValue()
                    || binding.buffer.HasValue() || binding.offset != 0 || binding.size != 0)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                images[i].imageView = record->view;
                images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                texturesSupportLinearFiltering &= record->linearFiltering;
                type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            }
            case RHIBindingType::Sampler: {
                const auto* record = registry.ResolveSampler(binding.sampler);
                if (!record || binding.texture.HasValue() || binding.buffer.HasValue() || binding.offset != 0 || binding.size != 0)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                images[i].sampler = record->sampler;
                requiresLinearFiltering |= record->linearFiltering;
                type = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            }
            case RHIBindingType::UniformBuffer: {
                const auto* record = registry.ResolveBuffer(binding.buffer);
                if (!record) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                LocalBufferState* local = nullptr;
                if (const auto status = GetLocalBufferState(binding.buffer, local); !status.IsSuccess()) return status;
                const auto localAccess = local->snapshot.state.access;
                const bool coversStages = localAccess.stage == RHIPipelineStage::AllCommands
                    || localAccess.stage == RHIPipelineStage::VertexFragmentShader
                    || (localAccess.stage == RHIPipelineStage::VertexShader && expected->visibility == RHIShaderVisibility::Vertex)
                    || (localAccess.stage == RHIPipelineStage::FragmentShader && expected->visibility == RHIShaderVisibility::Fragment);
                if (localAccess.access != RHIResourceAccess::UniformRead || !coversStages)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                if (const auto status = ValidateVulkanUniformBinding(binding, local->snapshot, m_Device.GetCapabilities()); !status.IsSuccess()) return status;
                buffers[i] = { record->buffer, binding.offset, binding.size };
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            }
            default: return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
            writes[i] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = binding.binding,
                .descriptorCount = 1,
                .descriptorType = type,
                .pImageInfo = binding.type == RHIBindingType::UniformBuffer ? nullptr : &images[i],
                .pBufferInfo = binding.type == RHIBindingType::UniformBuffer ? &buffers[i] : nullptr
            };
        }
        if (requiresLinearFiltering && !texturesSupportLinearFiltering)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        const size_t firstView=m_RetainedViews.size(), firstSampler=m_RetainedSamplers.size(), firstBuffer=m_RetainedBuffers.size();
        m_RetainedViews.reserve(firstView+bindings.size());
        m_RetainedSamplers.reserve(firstSampler+bindings.size());
        m_RetainedBuffers.reserve(firstBuffer+bindings.size());
        const auto fail = [this,firstView,firstSampler,firstBuffer](RHIStatus status) {
            ReleaseBuffers(firstBuffer); ReleaseSamplers(firstSampler); ReleaseTextureViews(firstView); return status;
        };
        for (const auto& binding : bindings) {
            RHIStatus status;
            switch (binding.type) {
            case RHIBindingType::SampledTexture: status=RetainTextureView(binding.texture); break;
            case RHIBindingType::Sampler: status=RetainSampler(binding.sampler); break;
            case RHIBindingType::UniformBuffer: status=RetainBuffer(binding.buffer); break;
            default: return fail({ .code=RHIErrorCode::InvalidDescription,.operation=operation });
            }
            if (!status.IsSuccess()) return fail(status);
        }
        VkDescriptorSet descriptorSet=VK_NULL_HANDLE;
        if (const auto status=m_Device.ObserveStatus(m_Descriptors.Allocate(m_BoundBindingLayout,descriptorSet));!status.IsSuccess()) return fail(status);
        for (auto& write:writes) write.dstSet=descriptorSet;
        vkUpdateDescriptorSets(m_Device.GetHandle(),static_cast<uint32_t>(bindings.size()),writes.data(),0,nullptr);
        vkCmdBindDescriptorSets(m_CommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,m_BoundPipelineLayout,0,1,&descriptorSet,0,nullptr);
        m_GraphicsBindingsSet=true;
        return {};
    }

    RHIStatus VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        constexpr const char* operation = "VulkanCommandList::Draw";

        if (m_State != RHICommandListState::Rendering
            || (m_BoundColorFormat == PixelFormat::Unknown && m_BoundDepthFormat == PixelFormat::Unknown)
            || m_BoundDepthFormat != m_RenderingDepthFormat || (m_RenderingDepthReadOnly && m_BoundDepthWrite)
            || m_BoundColorFormat != m_RenderingColorFormat || !m_ViewportAndScissorSet || !m_PushConstantsSet || !m_GraphicsBindingsSet)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (m_VertexStride != 0) {
            if (!m_VertexBufferBound)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            if (vertexCount != 0 && instanceCount != 0) {
                const uint64_t lastVertex = static_cast<uint64_t>(firstVertex) + vertexCount - 1;

                // lastVertex * stride + attributeEnd <= availableBytes。
                if (m_VertexAttributeEnd > m_VertexBufferBytes ||
                    lastVertex > (m_VertexBufferBytes - m_VertexAttributeEnd) / m_VertexStride)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
        }

        vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        return {};
    }

    RHIStatus VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        constexpr const char* operation = "VulkanCommandList::DrawIndexed";

        if (m_State != RHICommandListState::Rendering
            || (m_BoundColorFormat == PixelFormat::Unknown && m_BoundDepthFormat == PixelFormat::Unknown)
            || m_BoundDepthFormat != m_RenderingDepthFormat || (m_RenderingDepthReadOnly && m_BoundDepthWrite)
            || m_BoundColorFormat != m_RenderingColorFormat || !m_ViewportAndScissorSet || !m_PushConstantsSet || !m_GraphicsBindingsSet ||
            !m_IndexBufferBound || (m_VertexStride != 0 && !m_VertexBufferBound))
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        const uint64_t firstByte = static_cast<uint64_t>(firstIndex) * m_IndexSize;
        const uint64_t byteCount = static_cast<uint64_t>(indexCount) * m_IndexSize;

        if (firstByte > m_IndexBufferBytes || byteCount > m_IndexBufferBytes - firstByte)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        return {};
    }

    RHIStatus VulkanCommandList::PipelineBarrier(std::span<const RHIBufferBarrier> barriers)
    {
        constexpr const char* operation = "VulkanCommandList::PipelineBarrier(Buffer)";

        if (m_State != RHICommandListState::Recording)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (barriers.empty()) return {};

        if (barriers.size() > std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const auto& registry = m_Device.GetResourceRegistry();
        std::vector<VkBufferMemoryBarrier2> nativeBarriers;
        nativeBarriers.reserve(barriers.size());

        for (const auto& desc : barriers) {
            VkBufferMemoryBarrier2 native{};
            if (const RHIStatus status = CompileVulkanBufferBarrier(desc, registry, native); !status.IsSuccess()) return status;
            nativeBarriers.push_back(native);
        }

        const size_t first = m_RetainedBuffers.size();
        m_RetainedBuffers.reserve(first + barriers.size());
        m_LocalBufferStates.reserve(m_LocalBufferStates.size() + barriers.size());
        for (const auto& desc : barriers) {
            LocalBufferState* local = nullptr;
            if (const auto status = GetLocalBufferState(desc.buffer, local); !status.IsSuccess()) return status;
        }

        for (const auto& desc : barriers) {
            if (const RHIStatus status = RetainBuffer(desc.buffer); !status.IsSuccess()) {
                ReleaseBuffers(first);
                return status;
            }
        }

        const VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(nativeBarriers.size()),
            .pBufferMemoryBarriers = nativeBarriers.data()
        };

        vkCmdPipelineBarrier2(m_CommandBuffer, &dependencyInfo);
        // 容量已在原生命令前准备；以下发布局部录制状态不再分配。
        for (const auto& desc : barriers) {
            const auto found = std::ranges::find_if(m_LocalBufferStates,
                [&](const auto& entry) { return entry.buffer == desc.buffer; });
            found->snapshot.state.access = desc.after;
        }
        return {};
    }

    RHIStatus VulkanCommandList::CopyBuffer(BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy)
    {
        constexpr const char* operation = "VulkanCommandList::CopyBuffer";

        if (m_State != RHICommandListState::Recording)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        auto& registry = m_Device.GetResourceRegistry();
        const auto* sourceRecord = registry.ResolveBuffer(source);
        const auto* destinationRecord = registry.ResolveBuffer(destination);

        if (!sourceRecord || !destinationRecord || copy.size == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (!HasBufferUsage(sourceRecord->desc.usage, RHIBufferUsage::TransferSource) ||
            !HasBufferUsage(destinationRecord->desc.usage, RHIBufferUsage::TransferDestination))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (copy.sourceOffset > sourceRecord->desc.size || copy.size > sourceRecord->desc.size - copy.sourceOffset ||
            copy.destinationOffset > destinationRecord->desc.size || copy.size > destinationRecord->desc.size - copy.destinationOffset)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (sourceRecord->buffer == destinationRecord->buffer &&
            copy.sourceOffset < copy.destinationOffset + copy.size &&
            copy.destinationOffset < copy.sourceOffset + copy.size)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        m_LocalBufferStates.reserve(m_LocalBufferStates.size() + 2);
        LocalBufferState* sourceState = nullptr;
        LocalBufferState* destinationState = nullptr;
        if (const auto status = GetLocalBufferState(source, sourceState); !status.IsSuccess()) return status;
        if (const auto status = GetLocalBufferState(destination, destinationState); !status.IsSuccess()) return status;
        if (!RangeContains(sourceState->snapshot.state.initialized, copy.sourceOffset, copy.size))
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        auto& initialized = destinationState->snapshot.state.initialized;
        initialized.reserve(initialized.size() + 1);

        const VkBuffer sourceBuffer = sourceRecord->buffer;
        const VkBuffer destinationBuffer = destinationRecord->buffer;
        const size_t first = m_RetainedBuffers.size();
        m_RetainedBuffers.reserve(first + 2);

        if (const RHIStatus status = RetainBuffer(source); !status.IsSuccess()) return status;

        if (const RHIStatus status = RetainBuffer(destination); !status.IsSuccess()) {
            ReleaseBuffers(first);
            return status;
        }

        const VkBufferCopy region{
            .srcOffset = copy.sourceOffset,
            .dstOffset = copy.destinationOffset,
            .size = copy.size
        };

        vkCmdCopyBuffer(m_CommandBuffer, sourceBuffer, destinationBuffer, 1, &region);
        // 只有真实copy录制成功后的目标区间才初始化；此前已准备容量，不在副作用后分配。
        MarkInitialized(initialized, copy.destinationOffset, copy.size);
        return {};
    }

    RHIStatus VulkanCommandList::CopyBufferToTexture(
        BufferHandle source, TextureViewHandle destination, const RHIBufferTextureCopy& copy)
    {
        constexpr const char* operation = "VulkanCommandList::CopyBufferToTexture";

        if (m_State != RHICommandListState::Recording)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        auto& registry = m_Device.GetResourceRegistry();
        const auto* sourceRecord = registry.ResolveBuffer(source);
        const auto* destinationRecord = registry.ResolveTextureView(destination);

        if (!sourceRecord || !destinationRecord ||
            !HasBufferUsage(sourceRecord->desc.usage, RHIBufferUsage::TransferSource) ||
            !HasTextureUsage(destinationRecord->usage, RHITextureUsage::TransferDestination) ||
            destinationRecord->presentable || GetPixelFormatInfo(destinationRecord->format).isDepth || copy.bufferOffset % 4 != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (copy.range != RHITextureSubresourceRange{} || destinationRecord->sampleCount != 1)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        uint64_t bytes = 0;

        if (!TryGetTextureUploadByteSize(destinationRecord->extent, destinationRecord->format, bytes))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        if (!IsValidBufferRange(sourceRecord->desc.size, copy.bufferOffset, bytes))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        LocalBufferState* sourceState = nullptr;
        if (const auto status = GetLocalBufferState(source, sourceState); !status.IsSuccess()) return status;
        if (!RangeContains(sourceState->snapshot.state.initialized, copy.bufferOffset, bytes))
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        const VkBuffer sourceBuffer = sourceRecord->buffer;
        const VkImage destinationImage = destinationRecord->image;
        const Extent2D extent = destinationRecord->extent;
        const size_t firstBuffer = m_RetainedBuffers.size();

        if (const RHIStatus status = RetainBuffer(source); !status.IsSuccess()) return status;

        if (const RHIStatus status = RetainTextureView(destination); !status.IsSuccess()) {
            ReleaseBuffers(firstBuffer);
            return status;
        }

        const VkBufferImageCopy region{
            .bufferOffset = copy.bufferOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = copy.range.baseMipLevel,
                .baseArrayLayer = copy.range.baseArrayLayer,
                .layerCount = copy.range.layerCount
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { extent.width, extent.height, 1 }
        };

        vkCmdCopyBufferToImage(
            m_CommandBuffer, sourceBuffer, destinationImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        return {};
    }

    RHIStatus VulkanCommandList::BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset)
    {
        constexpr const char* operation = "VulkanCommandList::BindVertexBuffer";

        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (binding != 0)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const auto* record = m_Device.GetResourceRegistry().ResolveBuffer(buffer);

        if (!record || !HasBufferUsage(record->desc.usage, RHIBufferUsage::Vertex) ||
            offset >= record->desc.size || offset % 4 != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const VkBuffer nativeBuffer = record->buffer;
        const VkDeviceSize nativeOffset = offset;
        const uint64_t availableBytes = record->desc.size - offset;

        if (const RHIStatus status = RetainBuffer(buffer); !status.IsSuccess()) return status;

        vkCmdBindVertexBuffers(m_CommandBuffer, binding, 1, &nativeBuffer, &nativeOffset);
        m_VertexBufferBytes = availableBytes;
        m_VertexBufferBound = true;
        return {};
    }

    RHIStatus VulkanCommandList::BindIndexBuffer(BufferHandle buffer, RHIIndexType type, uint64_t offset)
    {
        constexpr const char* operation = "VulkanCommandList::BindIndexBuffer";

        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        VkIndexType nativeType{};
        uint32_t indexSize = 0;

        switch (type) {
        case RHIIndexType::UInt16: nativeType = VK_INDEX_TYPE_UINT16; indexSize = 2; break;
        case RHIIndexType::UInt32: nativeType = VK_INDEX_TYPE_UINT32; indexSize = 4; break;
        default: return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        const auto* record = m_Device.GetResourceRegistry().ResolveBuffer(buffer);

        if (!record || !HasBufferUsage(record->desc.usage, RHIBufferUsage::Index) ||
            offset >= record->desc.size || offset % indexSize != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const VkBuffer nativeBuffer = record->buffer;
        const uint64_t availableBytes = record->desc.size - offset;

        if (const RHIStatus status = RetainBuffer(buffer); !status.IsSuccess()) return status;

        vkCmdBindIndexBuffer(m_CommandBuffer, nativeBuffer, offset, nativeType);
        m_IndexBufferBytes = availableBytes;
        m_IndexSize = indexSize;
        m_IndexBufferBound = true;
        return {};
    }

    RHIStatus VulkanCommandList::RetainBuffer(BufferHandle handle)
    {
        if (std::ranges::find(m_RetainedBuffers, handle) != m_RetainedBuffers.end()) return {};

        m_RetainedBuffers.push_back(handle);
        const RHIStatus status = m_Device.GetResourceRegistry().RetainBuffer(handle);
        if (!status.IsSuccess()) m_RetainedBuffers.pop_back();
        return status;
    }

    void VulkanCommandList::ReleasePipelines() noexcept
    {
        while (!m_RetainedPipelines.empty()) {
            m_Device.GetResourceRegistry().ReleasePipeline(m_RetainedPipelines.back());
            m_RetainedPipelines.pop_back();
        }
    }

    void VulkanCommandList::ReleaseBuffers(size_t first) noexcept
    {
        while (m_RetainedBuffers.size() > first) {
            m_Device.GetResourceRegistry().ReleaseBufferReference(m_RetainedBuffers.back());
            m_RetainedBuffers.pop_back();
        }
    }

    bool VulkanCommandList::BeginDebugLabel(const char* name, const std::array<float, 4>& color)
    {
        if (m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering) {
            CAT_CORE_ASSERT(false, "Debug labels require a recording command list!");
            return false;
        }

        if (m_DebugLabelDepth == std::numeric_limits<uint32_t>::max()) {
            CAT_CORE_ASSERT(false, "Debug label depth exhausted!");
            return false;
        }

        if (!m_Device.GetDebugUtils().BeginLabel(m_CommandBuffer, name, color)) return false;

        ++m_DebugLabelDepth;
        return true;
    }

    void VulkanCommandList::EndDebugLabel()
    {
        if ((m_State != RHICommandListState::Recording && m_State != RHICommandListState::Rendering) || m_DebugLabelDepth == 0) {
            CAT_CORE_ASSERT(false, "Debug label has no matching begin in the recording command list!");
            return;
        }

        m_Device.GetDebugUtils().EndLabel(m_CommandBuffer);
        --m_DebugLabelDepth;
    }

}