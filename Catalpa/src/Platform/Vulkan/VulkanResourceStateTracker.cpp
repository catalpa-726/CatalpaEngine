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

    // 单层颜色/深度附件、上传、采样和呈现均经过同一已接受状态校验。
    static bool IsValidTextureState(const RHITextureState& state, const VulkanTextureRecord& texture) noexcept
    {
        const auto& source = state.access;
        VkPipelineStageFlags2 stage{};
        VkAccessFlags2 access{};
        VkImageLayout layout{};

        if (!ConvertVulkanTextureAccess(source, texture.desc.usage, texture.presentable, stage, access, layout))
            return false;

        const uint64_t subresources = uint64_t(texture.desc.mipLevels) * texture.desc.arrayLayers;
        if (!IsValidInitializedRanges(state.initializedSubresources, subresources)) return false;
        if (!state.initializedSubresources.empty()
            && state.initialized != RangeContains(state.initializedSubresources, 0, subresources)) return false;

        if (source.layout == RHITextureLayout::Undefined || source.layout == RHITextureLayout::Present)
            return !state.initialized && state.initializedSubresources.empty() && source.access == RHIResourceAccess::None &&
            (source.stage == RHIPipelineStage::None || source.stage == RHIPipelineStage::AllCommands);

        if (source.access == RHIResourceAccess::DepthRead && !state.initialized) return false;
        // 所有权专用批次没有payload访问，可保留layout/初始化而使用ALL_COMMANDS/NONE。
        return source.access != RHIResourceAccess::None || source.stage == RHIPipelineStage::AllCommands;
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
        const auto* viewSlot = registry.FindTextureView(view);

        if (!viewSlot || viewSlot->releaseRequested)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceStateTracker::QueryTexture" };

        // 父 owner 可以已释放；存活的 View 本身仍保留父图像。
        const auto* textureSlot = registry.FindTexture(viewSlot->record.texture);

        if (!textureSlot)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceStateTracker::QueryTexture" };

        snapshot = {
            .extent = viewSlot->record.extent,
            .format = viewSlot->record.format,
            .sampleCount = viewSlot->record.sampleCount,
            .state = textureSlot->state,
            .usage = textureSlot->record.desc.usage,
            .presentable = textureSlot->record.presentable,
            .mipLevels = textureSlot->record.desc.mipLevels,
            .arrayLayers = textureSlot->record.desc.arrayLayers
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

            // 已录制资源允许 owner 已释放，但命令引用必须仍然存在。
            const auto* slot = registry.FindBuffer(update.buffer);

            if (!slot || (!requireRetained && slot->releaseRequested))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            if (update.expectedRevision != slot->state.revision ||
                (requireRetained && slot->references == 0))
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            // 提交阶段只复核可能变化的状态。
            if (requireRetained) continue;

            for (size_t previous = 0; previous < i; ++previous) {
                if (buffers[previous].buffer == update.buffer)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }

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
            const auto* viewSlot = registry.FindTextureView(update.view);

            if (!viewSlot || (!requireRetained && viewSlot->releaseRequested))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            const auto* textureSlot = registry.FindTexture(viewSlot->record.texture);

            if (!textureSlot)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            if (update.expectedRevision != textureSlot->state.revision ||
                (requireRetained && viewSlot->references == 0))
                return { .code = RHIErrorCode::InvalidState, .operation = operation };

            // 提交阶段只复核身份、当前版本和命令引用。
            if (requireRetained) continue;

            for (size_t previous = 0; previous < i; ++previous) {
                if (textures[previous].view == update.view)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }

            if (update.expectedRevision == std::numeric_limits<uint64_t>::max())
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

            // 参与提交的交换链图像必须最终回到 Present。
            if (textureSlot->record.presentable && update.after.access.layout != RHITextureLayout::Present)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

            if (update.after.revision != update.expectedRevision + 1 ||
                !IsValidTextureState(update.after, textureSlot->record))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        return {};
    }

    void VulkanResourceStateTracker::CommitAccepted(
        VulkanResourceRegistry& registry,
        std::span<RHIBufferStateUpdate> buffers,
        std::span<RHITextureStateUpdate> textures, RHICompletionPoint completion) noexcept
    {
        // 当前提交和状态发布都在同一线程，Validate 与此处之间不插入其他状态修改。
        for (auto& update : buffers) {
            auto& slot = registry.m_Buffers[update.buffer.slot.index];

            CAT_CORE_ASSERT(slot.generation == update.buffer.slot.generation &&
                slot.state.revision == update.expectedRevision && slot.references != 0,
                "Accepted Buffer state transaction is no longer valid!");

            update.after.lastUse = completion;
            if (update.after.ownership.IsReleased())
                update.after.ownership.releasePoint = completion;
            else
                update.after.ownership.owner = completion.queue;
            if (update.writes) {
                update.after.frontier.writer = completion;
                update.after.frontier.readers.Clear();
            } else if (!update.after.frontier.readers.TryInclude(completion))
                std::terminate();
            std::swap(slot.state, update.after);
        }

        for (auto& update : textures) {
            const auto& viewSlot = registry.m_TextureViews[update.view.slot.index];
            const TextureHandle texture = viewSlot.record.texture;
            auto& textureSlot = registry.m_Textures[texture.slot.index];

            CAT_CORE_ASSERT(
                viewSlot.generation == update.view.slot.generation && viewSlot.references != 0 &&
                textureSlot.generation == texture.slot.generation && textureSlot.references != 0 &&
                textureSlot.state.revision == update.expectedRevision,
                "Accepted texture state transaction is no longer valid!");

            update.after.lastUse = completion;
            if (update.after.ownership.IsReleased())
                update.after.ownership.releasePoint = completion;
            else
                update.after.ownership.owner = completion.queue;
            if (update.writes) {
                update.after.frontier.writer = completion;
                update.after.frontier.readers.Clear();
            } else if (!update.after.frontier.readers.TryInclude(completion))
                std::terminate();
            std::swap(textureSlot.state, update.after);
        }
    }

}
