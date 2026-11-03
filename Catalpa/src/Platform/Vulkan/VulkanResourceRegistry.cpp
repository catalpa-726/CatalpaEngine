#include "catpch.h"
#include "Log/Log.h"
#include "VulkanResourceRegistry.h"
#include "VulkanDevice.h"
#include "VulkanDebugUtils.h"

namespace Catalpa {

    VulkanResourceRegistry::~VulkanResourceRegistry()
    {
        // 设备关闭前，帧命令已经销毁。先销毁 View，再归还父图像引用。
        for (auto& slot : m_TextureViews) {
            if (slot.record.view == VK_NULL_HANDLE) continue;

            CAT_CORE_ASSERT(slot.references == 0, "Texture view is still referenced by commands!");

            const TextureHandle texture = slot.record.texture;
            m_Retirement.RetireTextureView(std::exchange(slot.record, {}));
            ReleaseTextureReference(texture);
        }

        for (auto& slot : m_Textures) {
            CAT_CORE_ASSERT(slot.references == 0, "Texture is still referenced by views!");
            m_Retirement.RetireTexture(std::exchange(slot.record, {}));
        }

        for (auto& slot : m_Pipelines) {
            CAT_CORE_ASSERT(slot.references == 0, "Pipeline is still referenced by commands!");
            m_Retirement.RetirePipeline(std::exchange(slot.record, {}));
        }

        for (auto& slot : m_Buffers) {
            CAT_CORE_ASSERT(slot.references == 0, "Buffer is still referenced by commands!");
            m_Retirement.RetireBuffer(std::exchange(slot.record, {}));
        }
    }

    RHIStatus VulkanResourceRegistry::CreateTexture(const RHITextureDesc& desc, TextureHandle& texture)
    {
        texture = {};

        if (m_FirstFreeTexture == InvalidIndex) {
            if (m_Textures.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::CreateTexture" };

            m_Textures.reserve(m_Textures.size() + 1);
        }

        VulkanTextureRecord record{};
        if (const RHIStatus status = CreateVulkanTexture(m_Device, desc, record); !status.IsSuccess()) return status;

        uint32_t index = m_FirstFreeTexture;

        if (index == InvalidIndex) {
            index = static_cast<uint32_t>(m_Textures.size());
            m_Textures.emplace_back();
        }
        else {
            m_FirstFreeTexture = m_Textures[index].nextFree;
        }

        auto& slot = m_Textures[index];
        slot.nextFree = InvalidIndex;
        slot.references = 0;
        slot.releaseRequested = false;
        slot.record = record;
        slot.state = {};

        texture = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };
        return {};
    }

    RHIStatus VulkanResourceRegistry::CreateTextureView(TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view)
    {
        constexpr const char* operation = "VulkanResourceRegistry::CreateTextureView";
        view = {};

        const auto* found = FindTexture(texture);

        if (!found || found->releaseRequested || found->record.borrowed)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        // 仍被命令保留的旧 View 也属于活 View。
        if (found->references != 0)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        if (m_FirstFree == InvalidIndex) {
            if (m_TextureViews.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            m_TextureViews.reserve(m_TextureViews.size() + 1);
        }

        VulkanTextureViewRecord record{};
        if (const RHIStatus status = CreateVulkanTextureView(m_Device, texture, found->record, desc, record); !status.IsSuccess())
            return status;

        uint32_t index = m_FirstFree;

        if (index == InvalidIndex) {
            index = static_cast<uint32_t>(m_TextureViews.size());
            m_TextureViews.emplace_back();
        }
        else {
            m_FirstFree = m_TextureViews[index].nextFree;
        }

        auto& slot = m_TextureViews[index];
        slot.nextFree = InvalidIndex;
        slot.references = 0;
        slot.releaseRequested = false;
        slot.record = record;

        // 发布 View 时取得父引用；创建失败不会留下引用。
        ++m_Textures[texture.slot.index].references;

        view = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };
        return {};
    }

    const VulkanResourceRegistry::TextureSlot* VulkanResourceRegistry::FindTexture(TextureHandle texture) const noexcept
    {
        if (!texture.HasValue() || texture.device != m_DeviceId || texture.slot.index >= m_Textures.size()) return nullptr;

        const auto& slot = m_Textures[texture.slot.index];
        return slot.generation == texture.slot.generation && slot.record.image != VK_NULL_HANDLE ? &slot : nullptr;
    }

    const VulkanResourceRegistry::ViewSlot* VulkanResourceRegistry::FindTextureView(TextureViewHandle view) const noexcept
    {
        if (!view.HasValue() || view.device != m_DeviceId || view.slot.index >= m_TextureViews.size()) return nullptr;

        const auto& slot = m_TextureViews[view.slot.index];
        return slot.generation == view.slot.generation && slot.record.view != VK_NULL_HANDLE ? &slot : nullptr;
    }

    const VulkanTextureViewRecord* VulkanResourceRegistry::ResolveTextureView(TextureViewHandle view) const noexcept
    {
        const auto* slot = FindTextureView(view);
        return slot && !slot->releaseRequested ? &slot->record : nullptr;
    }

    void VulkanResourceRegistry::ReleaseTexture(TextureHandle texture) noexcept
    {
        const auto* found = FindTexture(texture);
        if (!found || found->record.borrowed || found->releaseRequested) return;

        auto& slot = m_Textures[texture.slot.index];
        slot.releaseRequested = true;
        if (slot.references == 0) RecycleTexture(texture.slot.index);
    }

    void VulkanResourceRegistry::ReleaseTextureView(TextureViewHandle view) noexcept
    {
        const auto* found = FindTextureView(view);

        // Acquire 返回的借用 View 由交换链持有。
        if (!found || found->record.borrowed || found->releaseRequested) return;

        auto& slot = m_TextureViews[view.slot.index];
        slot.releaseRequested = true;
        if (slot.references == 0) RecycleTextureView(view.slot.index);
    }

    RHIStatus VulkanResourceRegistry::RetainTextureView(TextureViewHandle view) noexcept
    {
        if (!ResolveTextureView(view))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RetainTextureView" };

        auto& references = m_TextureViews[view.slot.index].references;

        if (references == std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RetainTextureView" };

        ++references;
        return {};
    }

    void VulkanResourceRegistry::ReleaseTextureViewReference(TextureViewHandle view) noexcept
    {
        const auto* found = FindTextureView(view);
        CAT_CORE_ASSERT(found && found->references != 0, "Texture view has no retained reference!");

        auto& slot = m_TextureViews[view.slot.index];
        --slot.references;
        if (slot.releaseRequested && slot.references == 0) RecycleTextureView(view.slot.index);
    }

    void VulkanResourceRegistry::ReleaseTextureReference(TextureHandle texture) noexcept
    {
        const auto* found = FindTexture(texture);
        CAT_CORE_ASSERT(found && found->references != 0, "Texture has no retained view reference!");

        auto& slot = m_Textures[texture.slot.index];
        --slot.references;
        if (slot.releaseRequested && slot.references == 0) RecycleTexture(texture.slot.index);
    }

    void VulkanResourceRegistry::RecycleTextureView(uint32_t index) noexcept
    {
        auto& slot = m_TextureViews[index];
        CAT_CORE_ASSERT(slot.references == 0, "Cannot recycle a texture view still referenced by commands!");

        const TextureHandle texture = slot.record.texture;

        // 先完成原生 View 销毁，再允许父图像退休。
        m_Retirement.RetireTextureView(std::exchange(slot.record, {}));
        ReleaseTextureReference(texture);
        slot.releaseRequested = false;

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFree;
        m_FirstFree = index;
    }

    void VulkanResourceRegistry::RecycleTexture(uint32_t index) noexcept
    {
        auto& slot = m_Textures[index];
        CAT_CORE_ASSERT(slot.references == 0, "Cannot recycle a texture still referenced by views!");

        m_Retirement.RetireTexture(std::exchange(slot.record, {}));
        slot.releaseRequested = false;
        slot.state = {};

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFreeTexture;
        m_FirstFreeTexture = index;
    }

    RHIStatus VulkanResourceRegistry::RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle)
    {
        constexpr const char* operation = "VulkanResourceRegistry::RegisterBorrowedTextureView";
        handle = {};

        if (record.image == VK_NULL_HANDLE || record.view == VK_NULL_HANDLE) {
            CAT_CORE_ASSERT(false, "Texture view registration requires valid native objects!");
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        // 同一原生图像只能存在一个状态槽。
        if (std::any_of(m_Textures.begin(), m_Textures.end(), [&](const auto& slot) {
            return slot.record.image == record.image;
            }))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        // 发布槽位前，先准备好两个容器的容量。
        if (m_FirstFreeTexture == InvalidIndex) {
            if (m_Textures.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            m_Textures.reserve(m_Textures.size() + 1);
        }

        if (m_FirstFree == InvalidIndex) {
            if (m_TextureViews.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            m_TextureViews.reserve(m_TextureViews.size() + 1);
        }

        uint32_t textureIndex = m_FirstFreeTexture;

        if (textureIndex == InvalidIndex) {
            textureIndex = static_cast<uint32_t>(m_Textures.size());
            m_Textures.emplace_back();
        }
        else {
            m_FirstFreeTexture = m_Textures[textureIndex].nextFree;
        }

        auto& textureSlot = m_Textures[textureIndex];
        textureSlot.nextFree = InvalidIndex;
        textureSlot.references = 1;

        // 借用父图像不对外发放 owner，由唯一借用 View 保留至注销。
        textureSlot.releaseRequested = true;
        textureSlot.record = {};
        textureSlot.record.image = record.image;
        textureSlot.record.desc = {
            .extent = record.extent,
            .format = record.format,
            .usage = RHITextureUsage::ColorAttachment,
            .sampleCount = record.sampleCount
        };
        textureSlot.record.borrowed = true;
        textureSlot.record.presentable = true;
        textureSlot.state = {};

        const TextureHandle texture{
            .device = m_DeviceId,
            .slot = {.index = textureIndex, .generation = textureSlot.generation }
        };

        uint32_t viewIndex = m_FirstFree;

        if (viewIndex == InvalidIndex) {
            viewIndex = static_cast<uint32_t>(m_TextureViews.size());
            m_TextureViews.emplace_back();
        }
        else {
            m_FirstFree = m_TextureViews[viewIndex].nextFree;
        }

        record.usage = textureSlot.record.desc.usage;
        record.presentable = true;
        record.texture = texture;
        record.borrowed = true;

        auto& viewSlot = m_TextureViews[viewIndex];
        viewSlot.nextFree = InvalidIndex;
        viewSlot.references = 0;
        viewSlot.releaseRequested = false;
        viewSlot.record = record;

        handle = { .device = m_DeviceId, .slot = {.index = viewIndex, .generation = viewSlot.generation } };
        return {};
    }

    void VulkanResourceRegistry::UnregisterBorrowedTextureView(TextureViewHandle view) noexcept
    {
        const auto* found = FindTextureView(view);
        if (!found || !found->record.borrowed) return;

        if (found->references != 0) {
            CAT_CORE_ASSERT(false, "Cannot unregister a texture view still referenced by commands!");
            std::abort();
        }

        // 借用记录只撤销槽位，原生对象继续由 Swapchain 销毁。
        RecycleTextureView(view.slot.index);
    }

    RHIStatus VulkanResourceRegistry::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline)
    {
        pipeline = {};

        if (m_FirstFreePipeline == InvalidIndex) {
            if (m_Pipelines.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::CreateGraphicsPipeline" };

            m_Pipelines.reserve(m_Pipelines.size() + 1);
        }

        VulkanPipelineRecord record{};
        if (const RHIStatus status = CreateVulkanGraphicsPipeline(m_Device, desc, record); !status.IsSuccess()) return status;

        uint32_t index = m_FirstFreePipeline;

        if (index == InvalidIndex) {
            index = static_cast<uint32_t>(m_Pipelines.size());
            m_Pipelines.emplace_back();
        } else {
            m_FirstFreePipeline = m_Pipelines[index].nextFree;
        }

        auto& slot = m_Pipelines[index];
        slot.nextFree = InvalidIndex;
        slot.references = 0;
        slot.releaseRequested = false;
        slot.record = record;

        pipeline = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };

        if (const auto& debug = m_Device.GetDebugUtils(); debug.IsEnabled()) {
            const std::string name = "GraphicsPipeline[" + std::to_string(m_DeviceId.value) + ":" +
                std::to_string(index) + ":" + std::to_string(slot.generation) + "]";

            debug.NameObject(m_Device.GetHandle(), VK_OBJECT_TYPE_PIPELINE, VulkanHandleToUint64(record.pipeline), name.c_str());
            debug.NameObject(m_Device.GetHandle(), VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanHandleToUint64(record.layout), (name + ".Layout").c_str());
        }

        return {};
    }

    const VulkanResourceRegistry::PipelineSlot* VulkanResourceRegistry::FindPipeline(GraphicsPipelineHandle pipeline) const noexcept
    {
        if (!pipeline.HasValue() || pipeline.device != m_DeviceId || pipeline.slot.index >= m_Pipelines.size()) return nullptr;

        const auto& slot = m_Pipelines[pipeline.slot.index];
        return slot.generation == pipeline.slot.generation && slot.record.pipeline != VK_NULL_HANDLE ? &slot : nullptr;
    }

    const VulkanPipelineRecord* VulkanResourceRegistry::ResolveGraphicsPipeline(GraphicsPipelineHandle pipeline) const noexcept
    {
        const auto* slot = FindPipeline(pipeline);
        return slot && !slot->releaseRequested ? &slot->record : nullptr;
    }

    void VulkanResourceRegistry::ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept
    {
        if (!ResolveGraphicsPipeline(pipeline)) return;

        auto& slot = m_Pipelines[pipeline.slot.index];
        slot.releaseRequested = true;
        if (slot.references == 0) RecyclePipeline(pipeline.slot.index);
    }

    RHIStatus VulkanResourceRegistry::RetainPipeline(GraphicsPipelineHandle pipeline) noexcept
    {
        if (!ResolveGraphicsPipeline(pipeline))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RetainPipeline" };

        auto& references = m_Pipelines[pipeline.slot.index].references;

        if (references == std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RetainPipeline" };

        ++references;
        return {};
    }

    void VulkanResourceRegistry::ReleasePipeline(GraphicsPipelineHandle pipeline) noexcept
    {
        const auto* found = FindPipeline(pipeline);
        CAT_CORE_ASSERT(found && found->references != 0, "Pipeline has no retained reference!");

        auto& slot = m_Pipelines[pipeline.slot.index];
        --slot.references;
        if (slot.releaseRequested && slot.references == 0) RecyclePipeline(pipeline.slot.index);
    }

    void VulkanResourceRegistry::RecyclePipeline(uint32_t index) noexcept
    {
        auto& slot = m_Pipelines[index];
        m_Retirement.RetirePipeline(std::exchange(slot.record, {}));
        slot.releaseRequested = false;

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFreePipeline;
        m_FirstFreePipeline = index;
    }

    RHIStatus VulkanResourceRegistry::CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer)
    {
        buffer = {};

        if (m_FirstFreeBuffer == InvalidIndex) {
            if (m_Buffers.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::CreateBuffer" };

            m_Buffers.reserve(m_Buffers.size() + 1);
        }

        VulkanBufferRecord record{};
        if (const RHIStatus status = CreateVulkanBuffer(m_Device, desc, record); !status.IsSuccess()) return status;

        uint32_t index = m_FirstFreeBuffer;

        if (index == InvalidIndex) {
            index = static_cast<uint32_t>(m_Buffers.size());
            m_Buffers.emplace_back();
        }
        else {
            m_FirstFreeBuffer = m_Buffers[index].nextFree;
        }

        auto& slot = m_Buffers[index];
        slot.nextFree = InvalidIndex;
        slot.references = 0;
        slot.releaseRequested = false;
        slot.record = record;
        slot.state = {};

        buffer = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };

        if (const auto& debug = m_Device.GetDebugUtils(); debug.IsEnabled()) {
            const std::string name = std::string(desc.memory == RHIMemoryDomain::Upload ? "UploadBuffer[" : "DeviceBuffer[") +
                std::to_string(m_DeviceId.value) + ":" + std::to_string(index) + ":" + std::to_string(slot.generation) + "]";

            debug.NameObject(m_Device.GetHandle(), VK_OBJECT_TYPE_BUFFER, VulkanHandleToUint64(record.buffer), name.c_str());
        }

        return {};
    }

    const VulkanResourceRegistry::BufferSlot* VulkanResourceRegistry::FindBuffer(BufferHandle buffer) const noexcept
    {
        if (!buffer.HasValue() || buffer.device != m_DeviceId || buffer.slot.index >= m_Buffers.size()) return nullptr;

        const auto& slot = m_Buffers[buffer.slot.index];
        return slot.generation == buffer.slot.generation && slot.record.buffer != VK_NULL_HANDLE ? &slot : nullptr;
    }

    const VulkanBufferRecord* VulkanResourceRegistry::ResolveBuffer(BufferHandle buffer) const noexcept
    {
        const auto* slot = FindBuffer(buffer);
        return slot && !slot->releaseRequested ? &slot->record : nullptr;
    }

    RHIStatus VulkanResourceRegistry::WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data)
    {
        constexpr const char* operation = "VulkanResourceRegistry::WriteBuffer";
        const auto* found = FindBuffer(buffer);

        if (!found || found->releaseRequested)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        // 命令引用保留至安全 reset，因此同时禁止改写已录制和在飞数据。
        if (found->references != 0)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (found->record.desc.memory != RHIMemoryDomain::Upload)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };

        if (!IsValidBufferRange(found->record.desc.size, offset, static_cast<uint64_t>(data.size())))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (data.empty()) return {};

        auto& slot = m_Buffers[buffer.slot.index];
        auto& initialized = slot.state.initialized;

        if (slot.state.revision == std::numeric_limits<uint64_t>::max() || initialized.size() == initialized.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        // 在实际修改内容之前完成容量准备。
        initialized.reserve(initialized.size() + 1);

        const RHIStatus status = WriteVulkanBuffer(m_Device, slot.record, offset, data);
        ++slot.state.revision;

        if (status.IsSuccess()) {
            MarkInitialized(initialized, offset, static_cast<uint64_t>(data.size()));
        }
        else {
            // memcpy 已可能发生；flush 失败不能视为内容回滚。
            initialized.clear();
        }

        return status;
    }

    void VulkanResourceRegistry::ReleaseBuffer(BufferHandle buffer) noexcept
    {
        if (!ResolveBuffer(buffer)) return;

        auto& slot = m_Buffers[buffer.slot.index];
        slot.releaseRequested = true;
        if (slot.references == 0) RecycleBuffer(buffer.slot.index);
    }

    RHIStatus VulkanResourceRegistry::RetainBuffer(BufferHandle buffer) noexcept
    {
        if (!ResolveBuffer(buffer))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RetainBuffer" };

        auto& references = m_Buffers[buffer.slot.index].references;

        if (references == std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RetainBuffer" };

        ++references;
        return {};
    }

    void VulkanResourceRegistry::ReleaseBufferReference(BufferHandle buffer) noexcept
    {
        const auto* found = FindBuffer(buffer);
        CAT_CORE_ASSERT(found && found->references != 0, "Buffer has no retained reference!");

        auto& slot = m_Buffers[buffer.slot.index];
        --slot.references;
        if (slot.releaseRequested && slot.references == 0) RecycleBuffer(buffer.slot.index);
    }

    void VulkanResourceRegistry::RecycleBuffer(uint32_t index) noexcept
    {
        auto& slot = m_Buffers[index];
        m_Retirement.RetireBuffer(std::exchange(slot.record, {}));
        slot.state = {};
        slot.releaseRequested = false;

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFreeBuffer;
        m_FirstFreeBuffer = index;
    }

}