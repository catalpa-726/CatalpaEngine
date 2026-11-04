#include "catpch.h"
#include "Images/ImageValidation.h"

namespace Catalpa {
    namespace {
        // 先做除法上界检查，所有磁盘布局运算在uint64内显式拒绝溢出。
        bool Multiply(uint64_t a, uint64_t b, uint64_t& value) noexcept {
            if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) return false;
            value = a * b; return true;
        }
    }
    bool ValidateImageData(const ImageData& image, std::string& error)
    {
        const auto fail = [&](const char* message) { error = message; return false; };
        const auto info = GetPixelFormatInfo(image.format);
        if (info.bytesPerBlock == 0 || info.blockWidth == 0 || info.blockHeight == 0) return fail("Image format is unsupported");
        if (image.width == 0 || image.height == 0 || image.depth == 0 || image.arrayLayers == 0 || image.mipLevels == 0)
            return fail("Image dimensions, array layers and mip count must be nonzero");
        if (image.dimension != ImageDimension::Texture2D && image.dimension != ImageDimension::Texture3D && image.dimension != ImageDimension::Cube)
            return fail("Image dimension is invalid");
        if ((image.dimension != ImageDimension::Texture3D && image.depth != 1)
            || (image.dimension == ImageDimension::Texture3D && image.arrayLayers != 1)
            || (image.dimension == ImageDimension::Cube && image.width != image.height)) return fail("Image dimensions conflict with the declared texture kind");
        if (image.colorSpace != ImageColorSpace::Linear && image.colorSpace != ImageColorSpace::SRGB) return fail("Image color space is invalid");
        if (image.alphaMode != ImageAlphaMode::Opaque && image.alphaMode != ImageAlphaMode::Straight && image.alphaMode != ImageAlphaMode::Premultiplied)
            return fail("Image alpha mode is invalid");
        if (image.semantic != ImageSemantic::Color && image.semantic != ImageSemantic::Normal
            && image.semantic != ImageSemantic::Data && image.semantic != ImageSemantic::Depth) return fail("Image semantic is invalid");
        if (info.isSRGB != (image.colorSpace == ImageColorSpace::SRGB)) return fail("Image color space must match its explicit pixel format");
        if ((image.semantic == ImageSemantic::Depth) != info.isDepth) return fail("Depth formats require depth semantics exclusively");
        if (image.semantic != ImageSemantic::Color && image.colorSpace != ImageColorSpace::Linear) return fail("Normal/data/depth images must use linear encoding");
        if ((image.semantic == ImageSemantic::Color || image.semantic == ImageSemantic::Normal) && info.isInteger)
            return fail("Integer ID images cannot be declared as color or normals");
        if (image.semantic == ImageSemantic::Normal && info.channels < 2) return fail("Normal images require at least two channels");
        if ((info.channels < 4 || image.semantic == ImageSemantic::Depth) && image.alphaMode != ImageAlphaMode::Opaque)
            return fail("This image format or semantic has no alpha channel");
        uint32_t maxMipLevels = 1;
        for (uint32_t size = std::max({ image.width, image.height, image.depth }); size > 1; size >>= 1) ++maxMipLevels;
        if (image.mipLevels > maxMipLevels) return fail("Image mip count exceeds its dimensions");
        const uint32_t faces = image.dimension == ImageDimension::Cube ? 6u : 1u;
        uint64_t count = 0;
        if (!Multiply(image.arrayLayers, faces, count) || !Multiply(count, image.mipLevels, count)
            || count > 1048576 || count != image.subresources.size()) return fail("Image subresource set is incomplete or exceeds its limit");
        try {
            std::vector<bool> seen(static_cast<size_t>(count));
            std::vector<std::pair<uint64_t, uint64_t>> ranges;
            ranges.reserve(image.subresources.size());
            for (const auto& sub : image.subresources) {
                if (sub.mip >= image.mipLevels || sub.layer >= image.arrayLayers || sub.face >= faces)
                    return fail("Image subresource index is outside declared dimensions");
                const size_t key = (static_cast<size_t>(sub.layer) * faces + sub.face) * image.mipLevels + sub.mip;
                if (seen[key]) return fail("Image contains a duplicate mip/layer/face");
                seen[key] = true;
                const uint32_t width = std::max(1u, image.width >> sub.mip);
                const uint32_t height = std::max(1u, image.height >> sub.mip);
                const uint32_t depth = std::max(1u, image.depth >> sub.mip);
                if (sub.width != width || sub.height != height || sub.depth != depth) return fail("Image mip dimensions are inconsistent");
                const uint64_t blocksX = (uint64_t(width) + info.blockWidth - 1) / info.blockWidth;
                const uint64_t blocksY = (uint64_t(height) + info.blockHeight - 1) / info.blockHeight;
                uint64_t rowBytes = 0, minimumSlice = 0, length = 0;
                if (!Multiply(blocksX, info.bytesPerBlock, rowBytes) || sub.rowPitch < rowBytes
                    || sub.rowPitch % info.bytesPerBlock != 0 || !Multiply(sub.rowPitch, blocksY, minimumSlice)
                    || sub.slicePitch < minimumSlice || !Multiply(sub.slicePitch, depth, length)
                    || length != sub.byteLength || length == 0) return fail("Image pitch or byte length is invalid or overflows");
                if (sub.offset > image.pixels.size() || sub.byteLength > image.pixels.size() - sub.offset)
                    return fail("Image subresource bytes are truncated");
                ranges.emplace_back(sub.offset, sub.offset + sub.byteLength);
            }
            std::sort(ranges.begin(), ranges.end());
            for (size_t i = 1; i < ranges.size(); ++i) if (ranges[i].first < ranges[i - 1].second)
                return fail("Image subresource byte ranges overlap");
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool ValidateImageAsset(const ImageAsset& image, std::string& error)
    {
        if (image.sourceUri.size() > 4096 || image.sourceUri.find('\0') != std::string::npos) {
            error = "Image source URI is invalid"; return false;
        }
        return ValidateImageData(image.image, error);
    }
}
