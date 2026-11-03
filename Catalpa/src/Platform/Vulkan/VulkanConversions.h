#pragma once

#include "VulkanHeaders.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"
#include "RHI/RHIRenderingDesc.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHIShaderDesc.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

    [[nodiscard]] VkFormat ToVulkanFormat(PixelFormat format) noexcept;

    [[nodiscard]] VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept;
    [[nodiscard]] VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept;

    [[nodiscard]] VkBufferUsageFlags ToVulkanBufferUsage(RHIBufferUsage usage) noexcept;

    // 转换纹理用途；包含未知用途位时返回 0。
    [[nodiscard]] VkImageUsageFlags ToVulkanTextureUsage(RHITextureUsage usage) noexcept;

    [[nodiscard]] VkFormat ToVulkanFormat(VertexFormat format) noexcept;

    // 将引擎的参数可见范围转换为 Vulkan 着色阶段集合。
    [[nodiscard]] VkShaderStageFlags ToVulkanShaderStages(RHIShaderVisibility visibility) noexcept;

}