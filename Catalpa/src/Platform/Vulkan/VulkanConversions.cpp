#include "catpch.h"
#include "VulkanConversions.h"

namespace Catalpa {

    VkFormat ToVulkanFormat(PixelFormat format) noexcept
    {
        switch (format) {
            case PixelFormat::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case PixelFormat::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
            case PixelFormat::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

}