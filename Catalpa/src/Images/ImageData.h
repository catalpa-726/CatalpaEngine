#pragma once
#include "catpch.h"

#include "Images/ImageTypes.h"

namespace Catalpa {
	/* 每个层级、数组层和立方体面都有显式布局，行距和偏移均以字节计。三维图像的切片位于同一子资源中。 */
	struct ImageSubresource {
		uint32_t mip = 0, layer = 0, face = 0;
		uint32_t width = 0, height = 0, depth = 1;
		uint64_t offset = 0, rowPitch = 0, slicePitch = 0, byteLength = 0;
	};
	/* 拥有原样像素和布局，不隐式转换颜色、翻转图像、补mip或修改原件。 */
	struct ImageData {
		PixelFormat format = PixelFormat::Unknown;
		ImageDimension dimension = ImageDimension::Texture2D;
		uint32_t width = 0, height = 0, depth = 1, arrayLayers = 1, mipLevels = 1;
		ImageColorSpace colorSpace = ImageColorSpace::Linear;
		ImageAlphaMode alphaMode = ImageAlphaMode::Opaque;
		ImageSemantic semantic = ImageSemantic::Color;
		std::vector<ImageSubresource> subresources;
		std::vector<std::byte> pixels;
	};
}
