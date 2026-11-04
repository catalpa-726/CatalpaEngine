#include "catpch.h"
#include "Images/ImageAssetLoader.h"
#include "Images/ImageValidation.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
    bool ImageAssetLoader::EncodePayload(const ImageAsset& asset, std::vector<std::byte>& output, std::string& error) {
        try {
            if (!ValidateImageAsset(asset, error)) return false;
            const auto& image = asset.image; BinaryWriter writer;
            writer.U32(static_cast<uint32_t>(image.format)); writer.U32(static_cast<uint32_t>(image.dimension));
            writer.U32(image.width); writer.U32(image.height); writer.U32(image.depth); writer.U32(image.arrayLayers); writer.U32(image.mipLevels);
            writer.U32(static_cast<uint32_t>(image.colorSpace)); writer.U32(static_cast<uint32_t>(image.alphaMode)); writer.U32(static_cast<uint32_t>(image.semantic));
            writer.String(asset.sourceUri); writer.U32(static_cast<uint32_t>(image.subresources.size()));
            for (const auto& sub : image.subresources) {
                writer.U32(sub.mip); writer.U32(sub.layer); writer.U32(sub.face); writer.U32(sub.width); writer.U32(sub.height); writer.U32(sub.depth);
                writer.U64(sub.offset); writer.U64(sub.rowPitch); writer.U64(sub.slicePitch); writer.U64(sub.byteLength);
            }
            writer.U64(image.pixels.size()); writer.Bytes(image.pixels);
            if (!writer.Error().empty()) { error = writer.Error(); return false; }
            output = writer.Data(); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool ImageAssetLoader::DecodePayload(std::span<const std::byte> bytes, const AssetDecodeContext& context, AssetLoadResult& output, std::string& error) {
        try {
            if (context.cancellation.IsCancellationRequested()) { error = "Image decode cancelled"; return false; }
            BinaryReader reader(bytes); ImageAsset asset; auto& image = asset.image;
            uint32_t format, dimension, color, alpha, semantic, count;
            if (!reader.U32(format) || !reader.U32(dimension) || !reader.U32(image.width) || !reader.U32(image.height) || !reader.U32(image.depth) || !reader.U32(image.arrayLayers) || !reader.U32(image.mipLevels)
                || !reader.U32(color) || !reader.U32(alpha) || !reader.U32(semantic) || !reader.String(asset.sourceUri, 4096) || !reader.U32(count) || count > 65536) { error = "Invalid image payload header"; return false; }
            image.format = static_cast<PixelFormat>(format); image.dimension = static_cast<ImageDimension>(dimension);
            image.colorSpace = static_cast<ImageColorSpace>(color); image.alphaMode = static_cast<ImageAlphaMode>(alpha); image.semantic = static_cast<ImageSemantic>(semantic);
            // Leave half of admitted workspace for validator ranges/keys and publication bookkeeping.
            const size_t payloadBudget = context.maxResidentBytes / 2;
            const size_t baseBytes = sizeof(ImageAsset) + asset.sourceUri.capacity() + 1 + size_t(count) * sizeof(ImageSubresource);
            if (baseBytes > payloadBudget || count > reader.Remaining() / 56) { error = "Image layout exceeds decode budget or input"; return false; }
            image.subresources.resize(count);
            for (auto& sub : image.subresources) {
                if (!reader.U32(sub.mip) || !reader.U32(sub.layer) || !reader.U32(sub.face) || !reader.U32(sub.width) || !reader.U32(sub.height) || !reader.U32(sub.depth)
                    || !reader.U64(sub.offset) || !reader.U64(sub.rowPitch) || !reader.U64(sub.slicePitch) || !reader.U64(sub.byteLength)) { error = reader.Error(); return false; }
            }
            uint64_t pixelBytes; std::span<const std::byte> pixels;
            if (!reader.U64(pixelBytes) || pixelBytes > payloadBudget - baseBytes || pixelBytes > reader.Remaining() || !reader.Bytes(static_cast<size_t>(pixelBytes), pixels) || !reader.AtEnd()) { error = "Image pixel range exceeds input or decode budget"; return false; }
            if (context.cancellation.IsCancellationRequested()) { error = "Image decode cancelled"; return false; }
            image.pixels.assign(pixels.begin(), pixels.end());
            if (!ValidateImageAsset(asset, error)) return false;
            const size_t resident = sizeof(ImageAsset) + asset.sourceUri.capacity() + 1 + image.subresources.capacity() * sizeof(ImageSubresource) + image.pixels.capacity();
            if (resident > context.maxResidentBytes) { error = "Image resident allocation exceeds budget"; return false; }
            output = MakeAssetLoadResult<ImageAsset>(std::make_shared<const ImageAsset>(std::move(asset)), resident); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    AssetCodec ImageAssetLoader::Codec() {
        AssetCodec codec;
        if (!AssetType::TryParse("image", codec.loader.type)) throw std::logic_error("Invalid built-in image type key");
        codec.loader.cppType = std::type_index(typeid(ImageAsset));
        codec.loader.decode = [](const AssetContainer& container, const AssetDecodeContext& context, AssetLoadResult& output, std::string& message) {
            if (container.payloadVersion != 1) { message = "Unsupported image payload version"; return false; }
            return ImageAssetLoader::DecodePayload(container.payload, context, output, message);
        };
        codec.encode = [](const AssetLoadResult& value, std::vector<std::byte>& bytes, std::string& message) {
            if (value.cppType != std::type_index(typeid(ImageAsset)) || !value.value) { message = "Image codec payload type mismatch"; return false; }
            return ImageAssetLoader::EncodePayload(*std::static_pointer_cast<const ImageAsset>(value.value), bytes, message);
        };
        return codec;
    }
    bool RegisterImageAssetLoader(AssetManager& manager, std::string& error) {
        return manager.RegisterLoader(ImageAssetLoader::Codec().loader, error);
    }
}
