#include "catpch.h"
#include "VulkanSampler.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanSampler(VulkanDevice& device, const RHISamplerDesc& desc, VulkanSamplerRecord& record)
    {
        constexpr const char* operation = "CreateVulkanSampler";
        record = {};

        const auto validAddress = [](RHISamplerAddressMode value) {
            return value == RHISamplerAddressMode::Repeat || value == RHISamplerAddressMode::ClampToEdge
                || value == RHISamplerAddressMode::MirroredRepeat;
        };
        const auto nativeAddress = [](RHISamplerAddressMode value) {
            if (value == RHISamplerAddressMode::Repeat) return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            return value == RHISamplerAddressMode::MirroredRepeat ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        };
        if ((desc.minFilter != RHIFilter::Nearest && desc.minFilter != RHIFilter::Linear) ||
            (desc.magFilter != RHIFilter::Nearest && desc.magFilter != RHIFilter::Linear) ||
            (desc.mipFilter != RHIFilter::Nearest && desc.mipFilter != RHIFilter::Linear) ||
            !validAddress(desc.addressU) || !validAddress(desc.addressV) || !validAddress(desc.addressW) ||
            !std::isfinite(desc.minLod) || !std::isfinite(desc.maxLod) || !std::isfinite(desc.mipLodBias) ||
            desc.minLod < 0.0f || desc.maxLod < desc.minLod ||
            std::abs(desc.mipLodBias) > device.GetCapabilities().maxSamplerLodBias)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        VkCompareOp compare = VK_COMPARE_OP_ALWAYS;
        switch (desc.compareOp) {
        case RHICompareOp::Always: break;
        case RHICompareOp::Less: compare = VK_COMPARE_OP_LESS; break;
        case RHICompareOp::LessEqual: compare = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case RHICompareOp::Equal: compare = VK_COMPARE_OP_EQUAL; break;
        case RHICompareOp::Greater: compare = VK_COMPARE_OP_GREATER; break;
        case RHICompareOp::GreaterEqual: compare = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        default: return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        const VkSamplerCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = desc.magFilter == RHIFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .minFilter = desc.minFilter == RHIFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .mipmapMode = desc.mipFilter == RHIFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = nativeAddress(desc.addressU),
            .addressModeV = nativeAddress(desc.addressV),
            .addressModeW = nativeAddress(desc.addressW),
            .mipLodBias = desc.mipLodBias,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = desc.compareEnabled,
            .compareOp = compare,
            .minLod = desc.minLod,
            .maxLod = desc.maxLod,
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
                || (desc.maxLod > 0.0f && desc.mipFilter == RHIFilter::Linear)
        };

        return {};
    }

    void DestroyVulkanSampler(const VulkanDevice& device, VulkanSamplerRecord& record) noexcept
    {
        if (record.sampler != VK_NULL_HANDLE) vkDestroySampler(device.GetHandle(), record.sampler, nullptr);
        record = {};
    }

}
