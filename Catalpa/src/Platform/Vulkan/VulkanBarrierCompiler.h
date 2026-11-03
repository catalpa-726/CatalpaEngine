#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandTypes.h"
#include "RHI/RHIError.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

    class VulkanResourceRegistry;

    // 当前供图形队列使用；调用者提供正确的前后访问状态。
    [[nodiscard]] RHIStatus CompileVulkanTextureBarrier(const RHITextureBarrier& desc, const VulkanResourceRegistry& registry, VkImageMemoryBarrier2& barrier) noexcept;

    [[nodiscard]] RHIStatus CompileVulkanBufferBarrier(const RHIBufferBarrier& desc, const VulkanResourceRegistry& registry, VkBufferMemoryBarrier2& barrier) noexcept;

    // 共享 Buffer 访问转换与合法性检查，供屏障编码和状态提交校验复用。
    [[nodiscard]] bool ConvertVulkanBufferAccess(
        const RHIBufferAccess& source,
        RHIBufferUsage usage,
        VkPipelineStageFlags2& stage,
        VkAccessFlags2& access) noexcept;

    // 共享纹理访问转换与合法性检查，不推导未声明的依赖。
    [[nodiscard]] bool ConvertVulkanTextureAccess(
        const RHITextureAccess& source,
        RHITextureUsage usage,
        bool presentable,
        VkPipelineStageFlags2& stage,
        VkAccessFlags2& access,
        VkImageLayout& layout) noexcept;

}