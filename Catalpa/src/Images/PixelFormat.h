#pragma once

namespace Catalpa {

	/* 编号稳定；新增CPU格式不自动证明某个RHI支持创建/采样/附件用途。 */
	enum class PixelFormat {
		Unknown = 0,
		RGBA8_UNorm = 1,
		RGBA8_SRGB = 2,
		BGRA8_UNorm = 3,
		BGRA8_SRGB = 4,
		D32_Float = 5,
		R8_UNorm = 6,
		R16_Float = 7,
		RGBA16_Float = 8,
		RGBA32_Float = 9,
		R32_UInt = 10,
		BC1_UNorm = 11,
		BC1_SRGB = 12,
		BC3_UNorm = 13,
		BC3_SRGB = 14,
		BC5_UNorm = 15,
		BC7_UNorm = 16,
		BC7_SRGB = 17
	};

	struct PixelFormatInfo {
		unsigned int blockWidth = 0;
		unsigned int blockHeight = 0;
		unsigned int bytesPerBlock = 0;
		bool isSRGB = false;
		bool isDepth = false;
		unsigned int channels = 0;
		bool isInteger = false;
		bool isCompressed = false;
		bool isFloat = false;
	};

	/**
	 * @brief 返回像素格式信息。
	 *
	 * @param[in] format 需查询块大小与编码信息的像素格式。
	 *
	 * @return 每块字节数、块尺寸及格式属性；可用 bytesPerBlock 判断是否为已知格式。
	 */
	[[nodiscard]] constexpr PixelFormatInfo GetPixelFormatInfo(PixelFormat format) noexcept {
		switch (format) {
		case PixelFormat::RGBA8_UNorm:
		case PixelFormat::BGRA8_UNorm:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .channels = 4};
		case PixelFormat::RGBA8_SRGB:
		case PixelFormat::BGRA8_SRGB:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .isSRGB = true, .channels = 4};
		case PixelFormat::D32_Float:
			return {
			    .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .isDepth = true, .channels = 1, .isFloat = true};
		case PixelFormat::R8_UNorm:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 1, .channels = 1};
		case PixelFormat::R16_Float:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 2, .channels = 1, .isFloat = true};
		case PixelFormat::RGBA16_Float:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 8, .channels = 4, .isFloat = true};
		case PixelFormat::RGBA32_Float:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 16, .channels = 4, .isFloat = true};
		case PixelFormat::R32_UInt:
			return {.blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4, .channels = 1, .isInteger = true};
		case PixelFormat::BC1_UNorm:
			return {.blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 8, .channels = 4, .isCompressed = true};
		case PixelFormat::BC1_SRGB:
			return {.blockWidth = 4,
			    .blockHeight = 4,
			    .bytesPerBlock = 8,
			    .isSRGB = true,
			    .channels = 4,
			    .isCompressed = true};
		case PixelFormat::BC3_UNorm:
		case PixelFormat::BC7_UNorm:
			return {.blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 16, .channels = 4, .isCompressed = true};
		case PixelFormat::BC3_SRGB:
		case PixelFormat::BC7_SRGB:
			return {.blockWidth = 4,
			    .blockHeight = 4,
			    .bytesPerBlock = 16,
			    .isSRGB = true,
			    .channels = 4,
			    .isCompressed = true};
		case PixelFormat::BC5_UNorm:
			return {.blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 16, .channels = 2, .isCompressed = true};
		default:
			return {};
		}
	}

}
