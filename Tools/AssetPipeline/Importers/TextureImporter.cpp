#include "catpch.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "Images/ImageValidation.h"
#define STBI_NO_STDIO
// Assimp随包已有自己的stb实现；本TU私有链接，避免两份实现导出同名全局符号。
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Catalpa::Tools {
    std::pair<ImageSemantic, std::string> TextureImporter::MaterialChannel(std::string_view channel) {
        if (channel == "baseColor" || channel == "emissive")
            return {ImageSemantic::Color, "color"};
        if (channel == "normal")
            return {ImageSemantic::Normal, "normal"};
        return {ImageSemantic::Data, "data"};
    }

    bool TextureImporter::Decode(AssetBuildContext& ctx, std::span<const std::byte> bytes, std::string source,
                                 ImageSemantic semantic, ImageAsset& out, std::string& e) {
        if (!ctx.CheckCancelled(e))
            return false;
        if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max())) {
            e = "Image bytes are empty or exceed decoder limit";
            return false;
        }
        const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
        int w = 0, h = 0, c = 0;
        if (!stbi_info_from_memory(data, int(bytes.size()), &w, &h, &c) || w <= 0 || h <= 0 ||
            uint64_t(w) * uint64_t(h) > ctx.request.settings.maxWorkingBytes / 8) {
            e = "Invalid image dimensions or decoded image budget exceeded";
            return false;
        }
        if (stbi_is_16_bit_from_memory(data, int(bytes.size()))) {
            e = "16-bit source image requires an explicit precision-preserving importer; no silent RGBA8 "
                "conversion";
            return false;
        }
        if (stbi_is_hdr_from_memory(data, int(bytes.size()))) {
            e = "HDR source requires a float-preserving image importer";
            return false;
        }
        const auto pixelBytes = size_t(w) * size_t(h) * 4;
        if (pixelBytes > 80 * 1024 * 1024 - 1024 * 1024) {
            e = "RGBA8 image exceeds bounded cooked-image size; source is not resized";
            return false;
        }
        if (!ctx.ReserveWorking(pixelBytes, e) || !ctx.WorkspaceFits(pixelBytes, e))
            return false;
        std::unique_ptr<stbi_uc, void (*)(void*)> pixels(
            stbi_load_from_memory(data, int(bytes.size()), &w, &h, &c, 4), stbi_image_free);
        if (!pixels) {
            e = std::string("Image decode failed: ") +
                (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
            return false;
        }
        ImageAsset image;
        image.sourceUri = std::move(source);
        auto& v = image.image;
        v.width = uint32_t(w);
        v.height = uint32_t(h);
        v.semantic = semantic;
        v.colorSpace = semantic == ImageSemantic::Color ? ImageColorSpace::SRGB : ImageColorSpace::Linear;
        v.format = semantic == ImageSemantic::Color ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNorm;
        v.alphaMode = (c == 2 || c == 4) ? ImageAlphaMode::Straight : ImageAlphaMode::Opaque;
        const size_t size = size_t(w) * size_t(h) * 4;
        v.pixels.resize(size);
        std::memcpy(v.pixels.data(), pixels.get(), size);
        v.subresources.push_back(
            {0, 0, 0, v.width, v.height, 1, 0, uint64_t(w) * 4, uint64_t(size), uint64_t(size)});
        if (!ValidateImageAsset(image, e) || !ctx.CheckCancelled(e))
            return false;
        ctx.Diagnose(AssetDiagnosticLevel::Info, image.sourceUri,
                     "Decoded " + std::to_string(w) + "x" + std::to_string(h) +
                         " to RGBA8; no flip, resize, mip bake or gamma transform");
        out = std::move(image);
        return true;
    }

    bool TextureImporter::Import(AssetBuildContext& ctx, AssetImportResult& output, std::string& e) const {
        std::vector<std::byte> bytes;
        ImageAsset image;
        if (!ctx.Read(ctx.request.source, bytes, e) ||
            !Decode(ctx, bytes, AssetPathText(ctx.request.source), ImageSemantic::Color, image, e))
            return false;
        AssetImportResult result;
        result.primary = ctx.Identity("image/color");
        result.assets.push_back(
            {result.primary, "image/color", AssetPathText(ctx.request.source.filename()), std::move(image)});
        result.mappings.push_back(
            {result.primary, "image/color", AssetPathText(ctx.request.source.filename()), "image"});
        output = std::move(result);
        return true;
    }
}
