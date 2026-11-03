#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHICommandTypes.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class VulkanResourceRegistry;

    // 当前供图形队列使用；调用者提供正确的前后访问状态。
    [[nodiscard]] RHIStatus CompileVulkanTextureBarrier(const RHITextureBarrier& desc, const VulkanResourceRegistry& registry, VkImageMemoryBarrier2& barrier) noexcept;

}