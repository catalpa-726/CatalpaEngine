#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class VulkanDevice;

    // 发布后由 Registry 独占管理，记录本身不自动销毁。
    struct VulkanPipelineRecord {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
    };

    [[nodiscard]] RHIStatus CreateVulkanGraphicsPipeline(VulkanDevice& device, const RHIGraphicsPipelineDesc& desc, VulkanPipelineRecord& pipeline);

    // 用于创建失败的未发布记录，或退休系统已经允许销毁的记录。
    void DestroyVulkanPipeline(const VulkanDevice& device, VulkanPipelineRecord& pipeline) noexcept;

}