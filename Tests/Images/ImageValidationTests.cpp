#include "catpch.h"

#include "TestRunner.h"
#include "Images/ImageValidation.h"

namespace Catalpa::Tests {
	namespace {
		/**
		 * @brief 显式构造2x2 RGBA8字节布局，不依赖图像解码器或修改外部素材。
		 *
		 * @return 仅含一层 2×2 RGBA8 像素的测试图像。
		 */
		ImageData RgbaImage() {
			ImageData image;
			image.format = PixelFormat::RGBA8_UNorm;
			image.width = 2;
			image.height = 2;
			image.semantic = ImageSemantic::Color;
			image.pixels.resize(16);
			image.subresources.push_back({0, 0, 0, 2, 2, 1, 0, 8, 16, 16});
			return image;
		}
	}
	void RunImageValidationTests(TestContext& test) {
		std::string error;
		const auto good = RgbaImage();
		test.Check(ValidateImageData(good, error), "RGBA8 exact subresource layout is accepted");
		auto bad = good;
		bad.pixels.pop_back();
		const auto bytes = bad.pixels;
		test.Check(
		    !ValidateImageData(bad, error) && bad.pixels == bytes, "Truncated pixels fail without editing input bytes");
		bad = good;
		bad.subresources[0].rowPitch = 4;
		test.Check(!ValidateImageData(bad, error), "Pitch shorter than one row is rejected");
		bad = good;
		bad.subresources[0].slicePitch = 8;
		bad.subresources[0].byteLength = 8;
		test.Check(!ValidateImageData(bad, error), "Slice cannot omit required rows");
		bad = good;
		bad.subresources[0].offset = std::numeric_limits<uint64_t>::max();
		test.Check(!ValidateImageData(bad, error), "Byte offset overflow cannot wrap into valid storage");
		bad = good;
		bad.width = std::numeric_limits<uint32_t>::max();
		bad.height = std::numeric_limits<uint32_t>::max();
		bad.subresources[0].width = bad.width;
		bad.subresources[0].height = bad.height;
		bad.subresources[0].rowPitch = uint64_t(bad.width) * 4;
		test.Check(!ValidateImageData(bad, error), "Huge pitch times height is rejected before memory access");
		bad = good;
		bad.format = PixelFormat::RGBA8_SRGB;
		test.Check(!ValidateImageData(bad, error), "sRGB format cannot carry a linear declaration");
		bad.colorSpace = ImageColorSpace::SRGB;
		test.Check(ValidateImageData(bad, error), "Explicit sRGB color asset is valid");
		bad.semantic = ImageSemantic::Normal;
		test.Check(!ValidateImageData(bad, error), "Normal map cannot use sRGB conversion");
		bad = good;
		bad.format = PixelFormat::D32_Float;
		test.Check(!ValidateImageData(bad, error), "Depth format requires a depth semantic");
		bad.semantic = ImageSemantic::Depth;
		test.Check(ValidateImageData(bad, error), "Explicit D32 depth asset has valid storage");
		bad.alphaMode = ImageAlphaMode::Straight;
		test.Check(!ValidateImageData(bad, error), "Depth data cannot advertise alpha");
		bad = good;
		bad.format = PixelFormat::R32_UInt;
		test.Check(!ValidateImageData(bad, error), "Integer IDs cannot be color data");
		bad.semantic = ImageSemantic::Data;
		test.Check(ValidateImageData(bad, error), "Integer ID data is explicitly typed");
		auto mips = good;
		mips.mipLevels = 2;
		mips.pixels.resize(20);
		mips.subresources.push_back({1, 0, 0, 1, 1, 1, 16, 4, 4, 4});
		test.Check(ValidateImageData(mips, error), "Complete mip chain uses explicit non-overlapping offsets");
		bad = mips;
		bad.subresources[1].offset = 12;
		test.Check(!ValidateImageData(bad, error), "Mip byte overlap is rejected");
		bad = mips;
		bad.subresources[1].mip = 0;
		test.Check(!ValidateImageData(bad, error), "Duplicate mip key is rejected");
		bad = mips;
		bad.subresources[1].width = 2;
		test.Check(!ValidateImageData(bad, error), "Mip dimensions derive from the base image");
		bad = mips;
		bad.subresources.pop_back();
		test.Check(!ValidateImageData(bad, error), "Missing declared mip is rejected");
		bad = good;
		bad.mipLevels = 3;
		test.Check(!ValidateImageData(bad, error), "Mip count cannot exceed the base dimensions");
		auto cube = good;
		cube.dimension = ImageDimension::Cube;
		cube.pixels.resize(96);
		cube.subresources.clear();
		for (uint32_t face = 0; face < 6; ++face)
			cube.subresources.push_back({0, 0, face, 2, 2, 1, uint64_t(face) * 16, 8, 16, 16});
		test.Check(ValidateImageData(cube, error), "Cube requires six separately addressed faces");
		bad = cube;
		bad.subresources.back().face = 0;
		test.Check(!ValidateImageData(bad, error), "Cube duplicate face cannot conceal a missing face");
		auto array = good;
		array.arrayLayers = 2;
		array.pixels.resize(32);
		array.subresources.push_back({0, 1, 0, 2, 2, 1, 16, 8, 16, 16});
		test.Check(ValidateImageData(array, error), "Array layers have independently checked byte ranges");
		auto volume = good;
		volume.dimension = ImageDimension::Texture3D;
		volume.depth = 2;
		volume.pixels.resize(32);
		volume.subresources[0].depth = 2;
		volume.subresources[0].byteLength = 32;
		test.Check(ValidateImageData(volume, error), "3D texture byte length includes every depth slice");
		bad = volume;
		bad.arrayLayers = 2;
		test.Check(!ValidateImageData(bad, error), "3D texture arrays are explicitly unsupported");
		auto block = good;
		block.format = PixelFormat::BC1_UNorm;
		block.width = 5;
		block.height = 5;
		block.pixels.resize(32);
		block.subresources[0] = {0, 0, 0, 5, 5, 1, 0, 16, 32, 32};
		test.Check(ValidateImageData(block, error), "Partial edge BC blocks still allocate complete 4x4 blocks");
		bad = block;
		bad.subresources[0].rowPitch = 8;
		test.Check(!ValidateImageData(bad, error), "BC block row count uses ceiling division");
		ImageAsset asset{good, std::string("source\0hidden", 13)};
		test.Check(!ValidateImageAsset(asset, error), "Embedded NUL source URI is rejected");
	}
}
