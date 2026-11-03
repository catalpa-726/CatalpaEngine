#include "catpch.h"
#include "VulkanBarrierCompiler.h"
#include "VulkanResourceRegistry.h"
#include "RHI/RHIBufferDesc.h"

namespace Catalpa {

    // 显式转换已经声明的访问，不推导未声明的依赖。
    static bool ConvertTextureAccess(const RHITextureAccess& source, VkPipelineStageFlags2& stage, VkAccessFlags2& access, VkImageLayout& layout) noexcept
    {
        switch (source.stage) {
            case RHIPipelineStage::None: stage = VK_PIPELINE_STAGE_2_NONE; break;
            case RHIPipelineStage::ColorAttachmentOutput: stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; break;
            case RHIPipelineStage::AllCommands: stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; break;
            default: return false;
        }

        switch (source.access) {
            case RHIResourceAccess::None: access = VK_ACCESS_2_NONE; break;
            case RHIResourceAccess::ColorRead: access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT; break;
            case RHIResourceAccess::ColorWrite: access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; break;
            case RHIResourceAccess::ColorReadWrite: access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; break;
            default: return false;
        }

        switch (source.layout) {
            case RHITextureLayout::Undefined: layout = VK_IMAGE_LAYOUT_UNDEFINED; break;
            case RHITextureLayout::ColorAttachment: layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; break;
            case RHITextureLayout::Present: layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; break;
            default: return false;
        }

        return source.access == RHIResourceAccess::None || (source.stage != RHIPipelineStage::None && source.layout == RHITextureLayout::ColorAttachment);
    }

    RHIStatus CompileVulkanTextureBarrier(const RHITextureBarrier& desc, const VulkanResourceRegistry& registry, VkImageMemoryBarrier2& barrier) noexcept
    {
        constexpr const char* operation = "CompileVulkanTextureBarrier";
        barrier = {};

        const auto* record = registry.ResolveTextureView(desc.view);

        if (!record || desc.after.layout == RHITextureLayout::Undefined)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (desc.range != RHITextureSubresourceRange{} || record->sampleCount != 1 || GetPixelFormatInfo(record->format).bytesPerBlock == 0)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        VkImageMemoryBarrier2 native{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = record->image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = desc.range.baseMipLevel,
                .levelCount = desc.range.levelCount,
                .baseArrayLayer = desc.range.baseArrayLayer,
                .layerCount = desc.range.layerCount
            }
        };

        if (!ConvertTextureAccess(desc.before, native.srcStageMask, native.srcAccessMask, native.oldLayout) ||
            !ConvertTextureAccess(desc.after, native.dstStageMask, native.dstAccessMask, native.newLayout))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        barrier = native;
        return {};
    }

    bool ConvertVulkanBufferAccess(const RHIBufferAccess& source, RHIBufferUsage usage, VkPipelineStageFlags2& stage, VkAccessFlags2& access) noexcept
    {
        switch (source.stage) {
            case RHIPipelineStage::None: stage = VK_PIPELINE_STAGE_2_NONE; break;
            case RHIPipelineStage::Transfer: stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT; break;
            case RHIPipelineStage::VertexInput: stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT; break;
            case RHIPipelineStage::AllCommands: stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; break;
            default: return false;
        }

        RHIPipelineStage requiredStage{};
        RHIBufferUsage requiredUsage{};

        switch (source.access) {
            case RHIResourceAccess::None:
                access = VK_ACCESS_2_NONE;
                return true;
            case RHIResourceAccess::TransferRead:
                access = VK_ACCESS_2_TRANSFER_READ_BIT;
                requiredStage = RHIPipelineStage::Transfer;
                requiredUsage = RHIBufferUsage::TransferSource;
                break;
            case RHIResourceAccess::TransferWrite:
                access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                requiredStage = RHIPipelineStage::Transfer;
                requiredUsage = RHIBufferUsage::TransferDestination;
                break;
            case RHIResourceAccess::VertexRead:
                access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
                requiredStage = RHIPipelineStage::VertexInput;
                requiredUsage = RHIBufferUsage::Vertex;
                break;
            case RHIResourceAccess::IndexRead:
                access = VK_ACCESS_2_INDEX_READ_BIT;
                requiredStage = RHIPipelineStage::VertexInput;
                requiredUsage = RHIBufferUsage::Index;
                break;
            case RHIResourceAccess::VertexIndexRead:
                access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
                requiredStage = RHIPipelineStage::VertexInput;
                requiredUsage = RHIBufferUsage::Vertex | RHIBufferUsage::Index;
                break;
            case RHIResourceAccess::TransferReadWrite:
                access = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
                requiredStage = RHIPipelineStage::Transfer;
                requiredUsage = RHIBufferUsage::TransferSource | RHIBufferUsage::TransferDestination;
                break;
            case RHIResourceAccess::MemoryReadWrite:
                access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                return source.stage == RHIPipelineStage::AllCommands;
            default:
                return false;
        }

        return (source.stage == requiredStage || source.stage == RHIPipelineStage::AllCommands) &&
            HasBufferUsage(usage, requiredUsage);
    }

    RHIStatus CompileVulkanBufferBarrier(const RHIBufferBarrier& desc, const VulkanResourceRegistry& registry, VkBufferMemoryBarrier2& barrier) noexcept
    {
        constexpr const char* operation = "CompileVulkanBufferBarrier";
        barrier = {};

        const auto* record = registry.ResolveBuffer(desc.buffer);

        if (!record || desc.size == 0 || desc.offset > record->desc.size || desc.size > record->desc.size - desc.offset)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        VkBufferMemoryBarrier2 native{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = record->buffer,
            .offset = desc.offset,
            .size = desc.size
        };

        if (!ConvertVulkanBufferAccess(desc.before, record->desc.usage, native.srcStageMask, native.srcAccessMask) ||
            !ConvertVulkanBufferAccess(desc.after, record->desc.usage, native.dstStageMask, native.dstAccessMask))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        barrier = native;
        return {};
    }

}