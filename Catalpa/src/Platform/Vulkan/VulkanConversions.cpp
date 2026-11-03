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

    VkBufferUsageFlags ToVulkanBufferUsage(RHIBufferUsage usage) noexcept
    {
        constexpr RHIBufferUsage supported = RHIBufferUsage::TransferSource | RHIBufferUsage::TransferDestination |
            RHIBufferUsage::Vertex | RHIBufferUsage::Index;

        if ((static_cast<uint32_t>(usage) & ~static_cast<uint32_t>(supported)) != 0) return 0;

        VkBufferUsageFlags flags = 0;
        if (HasBufferUsage(usage, RHIBufferUsage::TransferSource)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (HasBufferUsage(usage, RHIBufferUsage::TransferDestination)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (HasBufferUsage(usage, RHIBufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (HasBufferUsage(usage, RHIBufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        return flags;
    }

    VkFormat ToVulkanFormat(VertexFormat format) noexcept
    {
        switch (format) {
        case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
        }
    }

    VkShaderStageFlags ToVulkanShaderStages(RHIShaderVisibility visibility) noexcept
    {
        switch (visibility) {
        case RHIShaderVisibility::None: return 0;
        case RHIShaderVisibility::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case RHIShaderVisibility::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case RHIShaderVisibility::VertexFragment: return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        default: return 0;
        }
    }

}