#include "catpch.h"
#include "Log/Log.h"
#include "VulkanResourceRegistry.h"
#include "VulkanDevice.h"
#include "VulkanDebugUtils.h"

namespace Catalpa {

    VulkanResourceRegistry::~VulkanResourceRegistry()
    {
        // 设备关闭前，帧命令已经销毁。
        for (auto& slot : m_Pipelines) {
            CAT_CORE_ASSERT(slot.references == 0, "Pipeline is still referenced by commands!");
            m_Retirement.RetirePipeline(std::exchange(slot.record, {}));
        }
        for (auto& slot : m_Buffers) {
            CAT_CORE_ASSERT(slot.references == 0, "Buffer is still referenced by commands!");
            m_Retirement.RetireBuffer(std::exchange(slot.record, {}));
        }
    }

    // 取得空闲槽位，保存信息并返回句柄
    RHIStatus VulkanResourceRegistry::RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle)
    {
        handle = {};

        if (record.image == VK_NULL_HANDLE || record.view == VK_NULL_HANDLE) {
            CAT_CORE_ASSERT(false, "Texture view registration requires valid native objects!");
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RegisterBorrowedTextureView" };
        }

        uint32_t index = m_FirstFree;

        if (index != InvalidIndex) {
            m_FirstFree = m_TextureViews[index].nextFree;
        } else {
            if (m_TextureViews.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RegisterBorrowedTextureView" };

            index = static_cast<uint32_t>(m_TextureViews.size());
            m_TextureViews.emplace_back();
        }

        auto& slot = m_TextureViews[index];
        slot.record = record;
        slot.nextFree = InvalidIndex;
        slot.references = 0;

        handle = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };
        return {};
    }

    // 拒绝空句柄、其他设备的句柄、旧代次和已经撤销的登记
    const VulkanTextureViewRecord* VulkanResourceRegistry::ResolveTextureView(TextureViewHandle handle) const noexcept
    {
        if (!handle.HasValue() || handle.device != m_DeviceId || handle.slot.index >= m_TextureViews.size()) return nullptr;

        const auto& slot = m_TextureViews[handle.slot.index];
        return slot.generation == handle.slot.generation && slot.record.view != VK_NULL_HANDLE ? &slot.record : nullptr;
    }

    // 使当前句柄失效，增加代次后回收槽位；代次达到上限的槽位不再复用，避免回绕
    void VulkanResourceRegistry::UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept
    {
        if (!ResolveTextureView(handle)) return;

        auto& slot = m_TextureViews[handle.slot.index];

        if (slot.references != 0) {
            CAT_CORE_ASSERT(false, "Cannot unregister a texture view still referenced by commands!");
            std::abort();
        }

        slot.record = {};

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFree;
        m_FirstFree = handle.slot.index;
    }

    RHIStatus VulkanResourceRegistry::RetainTextureView(TextureViewHandle handle) noexcept
    {
        if (!ResolveTextureView(handle))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RetainTextureView" };

        auto& references = m_TextureViews[handle.slot.index].references;

        if (references == std::numeric_limits<uint32_t>::max())
            return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RetainTextureView" };

        ++references;
        return {};
    }

    void VulkanResourceRegistry::ReleaseTextureView(TextureViewHandle handle) noexcept
    {
        CAT_CORE_ASSERT(ResolveTextureView(handle) && m_TextureViews[handle.slot.index].references != 0, "Texture view has no retained reference!");
        --m_TextureViews[handle.slot.index].references;
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
        const auto* slot = FindBuffer(buffer);

        if (!slot || slot->releaseRequested)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::WriteBuffer" };

        // 当前命令引用一直保留到安全 reset；因此这里同时禁止改写已录制和在飞数据。
        if (slot->references != 0)
            return { .code = RHIErrorCode::InvalidState, .operation = "VulkanResourceRegistry::WriteBuffer" };

        return WriteVulkanBuffer(m_Device, slot->record, offset, data);
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
        slot.releaseRequested = false;

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFreeBuffer;
        m_FirstFreeBuffer = index;
    }

}