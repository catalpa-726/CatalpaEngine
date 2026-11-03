#include "catpch.h"
#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include "VulkanConversions.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanTexture(VulkanDevice& device, const RHITextureDesc& desc, VulkanTextureRecord& record)
    {
        constexpr const char* operation = "CreateVulkanTexture";
        record = {};

        const VkFormat format = ToVulkanFormat(desc.format);
        const VkImageUsageFlags usage = ToVulkanTextureUsage(desc.usage);

        if (desc.extent.IsEmpty() || format == VK_FORMAT_UNDEFINED || usage == 0 ||
            desc.mipLevels == 0 || desc.arrayLayers == 0 || desc.sampleCount == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (desc.mipLevels != 1 || desc.arrayLayers != 1 || desc.sampleCount != 1)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        VkImageFormatProperties properties{};

        RHIStatus status = MakeVulkanStatus(
            vkGetPhysicalDeviceImageFormatProperties(
                device.GetPhysicalDevice(), format, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties),
            "vkGetPhysicalDeviceImageFormatProperties");

        if (!status.IsSuccess()) return status;

        if (desc.extent.width > properties.maxExtent.width ||
            desc.extent.height > properties.maxExtent.height ||
            properties.maxExtent.depth < 1 ||
            desc.mipLevels > properties.maxMipLevels ||
            desc.arrayLayers > properties.maxArrayLayers ||
            (properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const VkImageCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { desc.extent.width, desc.extent.height, 1 },
            .mipLevels = desc.mipLevels,
            .arrayLayers = desc.arrayLayers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        const VmaAllocationCreateInfo allocationInfo{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        };

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        status = MakeVulkanStatus(
            vmaCreateImage(
                device.GetMemoryAllocator().GetHandle(), &info, &allocationInfo,
                &image, &allocation, nullptr),
            "vmaCreateImage");

        if (!status.IsSuccess()) return status;

        record = {
            .image = image,
            .allocation = allocation,
            .desc = desc
        };

        return {};
    }

    RHIStatus CreateVulkanTextureView(
        VulkanDevice& device, TextureHandle texture, const VulkanTextureRecord& parent,
        const RHITextureViewDesc& desc, VulkanTextureViewRecord& record)
    {
        constexpr const char* operation = "CreateVulkanTextureView";
        record = {};

        if (desc.range.levelCount == 0 || desc.range.layerCount == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const PixelFormat format = desc.format == PixelFormat::Unknown ? parent.desc.format : desc.format;

        if (desc.range != RHITextureSubresourceRange{} || format != parent.desc.format)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        // 当前可创建 View 的用途为采样或颜色附件。
        if (!HasTextureUsage(parent.desc.usage, RHITextureUsage::Sampled) &&
            !HasTextureUsage(parent.desc.usage, RHITextureUsage::ColorAttachment))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };

        const VkImageViewCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = parent.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ToVulkanFormat(format),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = desc.range.baseMipLevel,
                .levelCount = desc.range.levelCount,
                .baseArrayLayer = desc.range.baseArrayLayer,
                .layerCount = desc.range.layerCount
            }
        };

        VkImageView view = VK_NULL_HANDLE;
        const RHIStatus status = MakeVulkanStatus(
            vkCreateImageView(device.GetHandle(), &info, nullptr, &view),
            "vkCreateImageView");

        if (!status.IsSuccess()) return status;

        record = {
            .image = parent.image,
            .view = view,
            .extent = parent.desc.extent,
            .format = format,
            .sampleCount = parent.desc.sampleCount,
            .usage = parent.desc.usage,
            .presentable = parent.presentable,
            .linearFiltering = device.GetFormatSupport(format).linearFiltering,
            .texture = texture
        };

        return {};
    }

    void DestroyVulkanTexture(const VulkanDevice& device, VulkanTextureRecord& record) noexcept
    {
        if (!record.borrowed && record.image != VK_NULL_HANDLE)
            vmaDestroyImage(device.GetMemoryAllocator().GetHandle(), record.image, record.allocation);

        record = {};
    }

    void DestroyVulkanTextureView(const VulkanDevice& device, VulkanTextureViewRecord& record) noexcept
    {
        if (!record.borrowed && record.view != VK_NULL_HANDLE)
            vkDestroyImageView(device.GetHandle(), record.view, nullptr);

        record = {};
    }

}