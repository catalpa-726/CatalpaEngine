#include "catpch.h"
#include "VulkanConversions.h"

namespace Catalpa {

	VkFormat ToVulkanFormat(PixelFormat format) noexcept {
		switch (format) {
		case PixelFormat::RGBA8_UNorm:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case PixelFormat::RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case PixelFormat::BGRA8_UNorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case PixelFormat::BGRA8_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
		case PixelFormat::D32_Float:
			return VK_FORMAT_D32_SFLOAT;
		case PixelFormat::R8_UNorm:
			return VK_FORMAT_R8_UNORM;
		case PixelFormat::R16_Float:
			return VK_FORMAT_R16_SFLOAT;
		case PixelFormat::RGBA16_Float:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case PixelFormat::RGBA32_Float:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case PixelFormat::R32_UInt:
			return VK_FORMAT_R32_UINT;
		case PixelFormat::BC1_UNorm:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case PixelFormat::BC1_SRGB:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
		case PixelFormat::BC3_UNorm:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case PixelFormat::BC3_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
		case PixelFormat::BC5_UNorm:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case PixelFormat::BC7_UNorm:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		case PixelFormat::BC7_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
		default:
			return VK_FORMAT_UNDEFINED;
		}
	}

	VkAttachmentLoadOp ToVulkanLoadOp(RHILoadOp operation) noexcept {
		switch (operation) {
		case RHILoadOp::Load:
			return VK_ATTACHMENT_LOAD_OP_LOAD;
		case RHILoadOp::Clear:
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		case RHILoadOp::DontCare:
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		default:
			return VK_ATTACHMENT_LOAD_OP_MAX_ENUM;
		}
	}

	VkAttachmentStoreOp ToVulkanStoreOp(RHIStoreOp operation) noexcept {
		switch (operation) {
		case RHIStoreOp::Store:
			return VK_ATTACHMENT_STORE_OP_STORE;
		case RHIStoreOp::DontCare:
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		default:
			return VK_ATTACHMENT_STORE_OP_MAX_ENUM;
		}
	}

	VkBufferUsageFlags ToVulkanBufferUsage(RHIBufferUsage usage) noexcept {
		constexpr RHIBufferUsage supported = RHIBufferUsage::TransferSource | RHIBufferUsage::TransferDestination |
		    RHIBufferUsage::Vertex | RHIBufferUsage::Index | RHIBufferUsage::Uniform | RHIBufferUsage::Storage |
		    RHIBufferUsage::Indirect;

		if ((static_cast<uint32_t>(usage) & ~static_cast<uint32_t>(supported)) != 0)
			return 0;

		VkBufferUsageFlags flags = 0;
		if (HasBufferUsage(usage, RHIBufferUsage::TransferSource))
			flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::TransferDestination))
			flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::Vertex))
			flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::Index))
			flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::Storage))
			flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::Indirect))
			flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		if (HasBufferUsage(usage, RHIBufferUsage::Uniform))
			flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		return flags;
	}

	VkImageUsageFlags ToVulkanTextureUsage(RHITextureUsage usage) noexcept {
		constexpr RHITextureUsage supported = RHITextureUsage::TransferSource | RHITextureUsage::TransferDestination |
		    RHITextureUsage::Sampled | RHITextureUsage::ColorAttachment | RHITextureUsage::DepthAttachment;

		if ((static_cast<uint32_t>(usage) & ~static_cast<uint32_t>(supported)) != 0)
			return 0;

		VkImageUsageFlags flags = 0;
		if (HasTextureUsage(usage, RHITextureUsage::TransferSource))
			flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (HasTextureUsage(usage, RHITextureUsage::TransferDestination))
			flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (HasTextureUsage(usage, RHITextureUsage::Sampled))
			flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (HasTextureUsage(usage, RHITextureUsage::ColorAttachment))
			flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (HasTextureUsage(usage, RHITextureUsage::DepthAttachment))
			flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		return flags;
	}

	VkFormat ToVulkanFormat(VertexFormat format) noexcept {
		switch (format) {
		case VertexFormat::Float2:
			return VK_FORMAT_R32G32_SFLOAT;
		case VertexFormat::Float3:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case VertexFormat::Float4:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		default:
			return VK_FORMAT_UNDEFINED;
		}
	}

	VkShaderStageFlags ToVulkanShaderStages(RHIShaderVisibility visibility) noexcept {
		switch (visibility) {
		case RHIShaderVisibility::Compute:
			return VK_SHADER_STAGE_COMPUTE_BIT;
		case RHIShaderVisibility::None:
			return 0;
		case RHIShaderVisibility::Vertex:
			return VK_SHADER_STAGE_VERTEX_BIT;
		case RHIShaderVisibility::Fragment:
			return VK_SHADER_STAGE_FRAGMENT_BIT;
		case RHIShaderVisibility::VertexFragment:
			return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		default:
			return 0;
		}
	}

}
