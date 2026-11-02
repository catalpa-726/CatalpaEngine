#pragma once

#include "VulkanHeaders.h"
#include "Images/PixelFormat.h"

namespace Catalpa {

    [[nodiscard]] VkFormat ToVulkanFormat(PixelFormat format) noexcept;

}