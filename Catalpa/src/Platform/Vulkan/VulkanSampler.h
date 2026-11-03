#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "RHI/RHISamplerDesc.h"

namespace Catalpa {

    class VulkanDevice;

    // 发布后由 Registry 管理，记录本身不自动销毁。
    struct VulkanSamplerRecord {
        VkSampler sampler = VK_NULL_HANDLE;
        bool linearFiltering = false;
    };

    // Registry 已检查仍存活的 sampler 数量上限。
    [[nodiscard]] RHIStatus CreateVulkanSampler(VulkanDevice& device, const RHISamplerDesc& desc, VulkanSamplerRecord& record);

    // 仅用于未发布记录，或退休系统已确认允许销毁的记录。
    void DestroyVulkanSampler(const VulkanDevice& device, VulkanSamplerRecord& record) noexcept;

}
