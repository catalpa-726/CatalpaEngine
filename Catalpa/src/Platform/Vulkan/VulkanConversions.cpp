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

    VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept
    {
        switch (operation) {
        case RHILoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case RHILoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case RHILoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_MAX_ENUM;
        }
    }

    VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept
    {
        switch (operation) {
        case RHIStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case RHIStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_MAX_ENUM;
        }
    }

}