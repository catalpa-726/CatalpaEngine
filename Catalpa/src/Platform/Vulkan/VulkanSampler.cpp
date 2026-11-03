#include "catpch.h"
#include "VulkanSampler.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanSampler(VulkanDevice& device, const RHISamplerDesc& desc, VulkanSamplerRecord& record)
    {
        constexpr const char* operation = "CreateVulkanSampler";
        record = {};

        if ((desc.minFilter != RHIFilter::Nearest && desc.minFilter != RHIFilter::Linear) ||
            (desc.magFilter != RHIFilter::Nearest && desc.magFilter != RHIFilter::Linear) ||
            (desc.addressU != RHISamplerAddressMode::Repeat && desc.addressU != RHISamplerAddressMode::ClampToEdge) ||
            (desc.addressV != RHISamplerAddressMode::Repeat && desc.addressV != RHISamplerAddressMode::ClampToEdge))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        const VkSamplerCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = desc.magFilter == RHIFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .minFilter = desc.minFilter == RHIFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = desc.addressU == RHISamplerAddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = desc.addressV == RHISamplerAddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            .unnormalizedCoordinates = VK_FALSE
        };

        VkSampler sampler = VK_NULL_HANDLE;
        const RHIStatus status = MakeVulkanStatus(
            vkCreateSampler(device.GetHandle(), &info, nullptr, &sampler),
            "vkCreateSampler");

        if (!status.IsSuccess()) return status;

        record = {
            .sampler = sampler,
            .linearFiltering = desc.minFilter == RHIFilter::Linear || desc.magFilter == RHIFilter::Linear
        };

        return {};
    }

    void DestroyVulkanSampler(const VulkanDevice& device, VulkanSamplerRecord& record) noexcept
    {
        if (record.sampler != VK_NULL_HANDLE) vkDestroySampler(device.GetHandle(), record.sampler, nullptr);
        record = {};
    }

}
