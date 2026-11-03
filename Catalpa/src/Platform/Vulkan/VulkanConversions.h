#pragma once

#include "VulkanHeaders.h"
#include "Images/PixelFormat.h"
#include "RHI/RHIRenderingDesc.h"

namespace Catalpa {

    [[nodiscard]] VkFormat ToVulkanFormat(PixelFormat format) noexcept;

    [[nodiscard]] VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept;
    [[nodiscard]] VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept;

}