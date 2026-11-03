#include "catpch.h"
#include "VulkanResourceStateTracker.h"
#include "VulkanResourceRegistry.h"
#include "VulkanBarrierCompiler.h"
#include "Log/Log.h"

// 实现版本冲突检查，提交后的状态交换
namespace Catalpa {

    // 校验初始化区间已排序、已合并，并且没有越过 Buffer 边界。
    static bool IsValidInitializedRanges(std::span<const RHIBufferRange> ranges, uint64_t capacity) noexcept
    {
        uint64_t previousEnd = 0;
        bool first = true;

        for (const auto& range : ranges) {
            if (range.size == 0 || !IsValidBufferRange(capacity, range.offset, range.size)) return false;
            if (!first && range.offset <= previousEnd) return false;

            previousEnd = range.offset + range.size;
            first = false;
        }

        return true;
    }

    // 当前仅允许单层颜色附件及交换链呈现状态。
    static bool IsValidTextureState(const RHITextureState& state) noexcept
    {
        const auto& access = state.access;

        switch (access.layout) {
        case RHITextureLayout::Undefined:
        case RHITextureLayout::Present:
            return !state.initialized && access.access == RHIResourceAccess::None &&
                (access.stage == RHIPipelineStage::None || access.stage == RHIPipelineStage::AllCommands);

        case RHITextureLayout::ColorAttachment:
            return (access.stage == RHIPipelineStage::ColorAttachmentOutput || access.stage == RHIPipelineStage::AllCommands) &&
                (access.access == RHIResourceAccess::ColorRead || access.access == RHIResourceAccess::ColorWrite ||
                    access.access == RHIResourceAccess::ColorReadWrite);

        default:
            return false;
        }
    }

    RHIStatus VulkanResourceStateTracker::Query(const VulkanResourceRegistry& registry, BufferHandle buffer, RHIBufferStateSnapshot& snapshot)
    {
        snapshot = {};
        const auto* slot = registry.FindBuffer(buffer);

        if (!slot || slot->releaseRequested)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceStateTracker::QueryBuffer" };

        snapshot = { .desc = slot->record.desc, .state = slot->state };
        return {};
    }

    RHIStatus VulkanResourceStateTracker::Query(const VulkanResourceRegistry& registry, TextureViewHandle view, RHITextureStateSnapshot& snapshot)
    {
        snapshot = {};
        const auto* record = registry.ResolveTextureView(view);

        if (!record)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceStateTracker::QueryTexture" };

        const auto& slot = registry.m_TextureViews[view.slot.index];

        snapshot = {
            .extent = record->extent,
            .format = record->format,
            .sampleCount = record->sampleCount,
            .state = slot.state
        };

        return {};
    }

    RHIStatus VulkanResourceStateTracker::Validate(
        const VulkanResourceRegistry& registry,
        std::span<const RHIBufferStateUpdate> buffers,
        std::span<const RHITextureStateUpdate> textures,
        bool requireRetained) noexcept
    {
        constexpr const char* operation = "VulkanResourceStateTracker::Validate";

        for (size_t i = 0; i < buffers.size(); ++i) {
            const auto& update = buffers[i];

            for (size_t previous = 0; previous < i; ++previous) {
                if (buffers[previous].buffer == update.buffer)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }

            // 已录制资源允许 owner 已释放，但命令引用必须仍然存在。
            const auto* slot = registry.FindBuffer(update.buffer);

            if (!slot || (!requireRetained && slot->releaseRequested))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            if (requireRetained && slot->references == 0)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            if (update.expectedRevision == 0 || update.expectedRevision != slot->state.revision)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            if (update.expectedRevision == std::numeric_limits<uint64_t>::max())
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            VkPipelineStageFlags2 stage{};
            VkAccessFlags2 access{};

            if (update.after.revision != update.expectedRevision + 1 ||
                !ConvertVulkanBufferAccess(update.after.access, slot->record.desc.usage, stage, access) ||
                !IsValidInitializedRanges(update.after.initialized, slot->record.desc.size))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& update = textures[i];

            for (size_t previous = 0; previous < i; ++previous) {
                if (textures[previous].view == update.view)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }

            if (!registry.ResolveTextureView(update.view))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            const auto& slot = registry.m_TextureViews[update.view.slot.index];

            if (requireRetained && slot.references == 0)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            if (update.expectedRevision == 0 || update.expectedRevision != slot.state.revision)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            if (update.expectedRevision == std::numeric_limits<uint64_t>::max())
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            if (update.after.revision != update.expectedRevision + 1 || !IsValidTextureState(update.after))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        return {};
    }

    void VulkanResourceStateTracker::CommitAccepted(
        VulkanResourceRegistry& registry,
        std::span<RHIBufferStateUpdate> buffers,
        std::span<RHITextureStateUpdate> textures) noexcept
    {
        // 当前提交和状态发布都在同一线程，Validate 与此处之间不插入其他状态修改。
        for (auto& update : buffers) {
            auto& slot = registry.m_Buffers[update.buffer.slot.index];

            CAT_CORE_ASSERT(slot.generation == update.buffer.slot.generation &&
                slot.state.revision == update.expectedRevision && slot.references != 0,
                "Accepted Buffer state transaction is no longer valid!");

            std::swap(slot.state, update.after);
        }

        for (auto& update : textures) {
            auto& slot = registry.m_TextureViews[update.view.slot.index];

            CAT_CORE_ASSERT(slot.generation == update.view.slot.generation &&
                slot.state.revision == update.expectedRevision && slot.references != 0,
                "Accepted texture state transaction is no longer valid!");

            std::swap(slot.state, update.after);
        }
    }

}