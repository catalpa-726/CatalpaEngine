#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIBindingDesc.h"
#include "RHI/RHIResourceState.h"
#include "RHI/RHICapabilities.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    // 创建前校验唯一set0布局及每stage/set上限；输出只借用，owner仍为PipelineRecord。
    [[nodiscard]] RHIStatus BuildVulkanBindingLayout(std::span<const RHIBindingLayoutEntry> entries,
        const VkPhysicalDeviceLimits& limits, uint32_t colorAttachmentCount,
        std::span<VkDescriptorSetLayoutBinding> output) noexcept;
    // 检查统一缓冲的类型、对齐、范围与初始化，不创建或持有另一份资源。
    [[nodiscard]] RHIStatus ValidateVulkanUniformBinding(const RHIBinding& binding,
        const RHIBufferStateSnapshot& snapshot, const RHICapabilities& capabilities) noexcept;
}
