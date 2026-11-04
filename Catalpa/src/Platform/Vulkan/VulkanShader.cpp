#include "catpch.h"
#include "VulkanShader.h"
#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanShader(VulkanDevice& device, const RHIShaderDesc& desc, VulkanShaderRecord& shader)
    {
        shader = {};

        std::string error;
        if (!ValidateRHIShader(desc, error))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanShader/metadata" };
        const auto& bytecode = desc.asset.Get()->spirv;

        VkShaderStageFlagBits stage{};

        switch (desc.stage) {
        case RHIShaderStage::Vertex: stage = VK_SHADER_STAGE_VERTEX_BIT; break;
        case RHIShaderStage::Fragment: stage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
        default: return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanShader" };
        }

        const VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = bytecode.size() * sizeof(uint32_t),
            .pCode = bytecode.data()
        };

        const RHIStatus status = MakeVulkanStatus(vkCreateShaderModule(device.GetHandle(), &info, nullptr, &shader.module), "vkCreateShaderModule");

        if (status.IsSuccess()) {
            shader.stage = stage;
            shader.entryPoint = desc.entryPoint;
        }

        return status;
    }

    void DestroyVulkanShader(const VulkanDevice& device, VulkanShaderRecord& shader) noexcept
    {
        if (shader.module != VK_NULL_HANDLE) vkDestroyShaderModule(device.GetHandle(), shader.module, nullptr);
        shader = {};
    }

}