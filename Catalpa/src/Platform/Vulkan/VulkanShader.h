#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIShaderDesc.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class VulkanDevice;

    // 只用于同步构建管线，不发布给上层。
    struct VulkanShaderRecord {
        VkShaderModule module = VK_NULL_HANDLE;
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
        const char* entryPoint = nullptr;
    };

    [[nodiscard]] RHIStatus CreateVulkanShader(VulkanDevice& device, const RHIShaderDesc& desc, VulkanShaderRecord& shader);
    void DestroyVulkanShader(const VulkanDevice& device, VulkanShaderRecord& shader) noexcept;

}