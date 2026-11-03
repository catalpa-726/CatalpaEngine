#pragma once

#include "VulkanHeaders.h"
#include "Images/PixelFormat.h"
#include "RHI/RHIRenderingDesc.h"
#include "RHI/RHIBufferDesc.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    [[nodiscard]] VkFormat ToVulkanFormat(PixelFormat format) noexcept;

    [[nodiscard]] VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept;
    [[nodiscard]] VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept;

    [[nodiscard]] VkBufferUsageFlags ToVulkanBufferUsage(RHIBufferUsage usage) noexcept;

    [[nodiscard]] VkFormat ToVulkanFormat(VertexFormat format) noexcept;

}