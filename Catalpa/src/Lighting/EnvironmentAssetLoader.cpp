#include "catpch.h"
#include "Lighting/EnvironmentAssetLoader.h"
#include "Images/ImageAssetLoader.h"
#include "Images/ImageValidation.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa {

    bool ValidateEnvironmentAsset(const EnvironmentAsset& asset, std::string& error) {
        if (asset.schemaVersion != 1 || asset.preprocessingVersion != 2 || asset.sampleCount < 16 || asset.sampleCount > 4096) {
            error = "Unsupported environment schema";
            return false;
        }
        const ImageAsset* images[]{&asset.radiance, &asset.irradiance, &asset.specular, &asset.brdfLut};
        for (const auto* item : images) {
            if (!ValidateImageAsset(*item, error))
                return false;
            const auto& image = item->image;
            if (image.format != PixelFormat::RGBA32_Float || image.colorSpace != ImageColorSpace::Linear ||
                image.dimension != (item == &asset.brdfLut ? ImageDimension::Texture2D : ImageDimension::Cube) || image.arrayLayers != 1 ||
                image.alphaMode != ImageAlphaMode::Opaque) {
                error = "Environment requires linear opaque RGBA32F Cube maps and a 2D BRDF LUT";
                return false;
            }
            const float maximum = item == &asset.irradiance ? 4e12f : item == &asset.brdfLut ? 1e7f : 1e12f;
            for (size_t offset = 0; offset + sizeof(float) <= image.pixels.size(); offset += sizeof(float)) {
                float value;
                std::memcpy(&value, image.pixels.data() + offset, sizeof(value));
                if (!std::isfinite(value) || value < 0 || value > maximum) {
                    error = "Environment image exceeds its nonnegative finite radiance/LUT budget";
                    return false;
                }
            }
        }
        uint32_t expectedSpecularLevels = 1;
        for (uint32_t size = asset.specular.image.width; size > 1; size >>= 1) ++expectedSpecularLevels;
        if (asset.radiance.image.width != asset.radiance.image.height ||
            asset.irradiance.image.width != asset.irradiance.image.height ||
            asset.specular.image.width != asset.specular.image.height ||
            asset.brdfLut.image.width != asset.brdfLut.image.height ||
            asset.radiance.image.mipLevels != 1 || asset.irradiance.image.mipLevels != 1 ||
            asset.brdfLut.image.mipLevels != 1 || asset.specular.image.mipLevels != expectedSpecularLevels) {
            error = "Environment requires square Cube faces and a square BRDF LUT";
            return false;
        }
        error.clear();
        return true;
    }

    bool EnvironmentAssetLoader::EncodePayload(const EnvironmentAsset& asset,
        std::vector<std::byte>& output, std::string& error) try {
        if (!ValidateEnvironmentAsset(asset, error))
            return false;
        BinaryWriter writer(80 * 1024 * 1024);
        writer.U32(1);
        writer.U32(asset.preprocessingVersion);
        writer.U32(asset.sampleCount);
        const ImageAsset* images[]{&asset.radiance, &asset.irradiance, &asset.specular, &asset.brdfLut};
        for (const auto* image : images) {
            std::vector<std::byte> bytes;
            if (!ImageAssetLoader::EncodePayload(*image, bytes, error))
                return false;
            writer.U64(bytes.size());
            writer.Bytes(bytes);
            if (!writer.Error().empty()) {
                error = writer.Error();
                return false;
            }
        }
        output = writer.Data();
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool EnvironmentAssetLoader::DecodePayload(std::span<const std::byte> bytes,
        const AssetDecodeContext& context, AssetLoadResult& output, std::string& error) try {
        if (context.cancellation.IsCancellationRequested()) {
            error = "Environment decode cancelled";
            return false;
        }
        BinaryReader reader(bytes);
        EnvironmentAsset candidate;
        if (!reader.U32(candidate.schemaVersion) || !reader.U32(candidate.preprocessingVersion) ||
            !reader.U32(candidate.sampleCount) || candidate.schemaVersion != 1 || candidate.preprocessingVersion != 2) {
            error = "Invalid environment payload version";
            return false;
        }
        size_t resident = sizeof(EnvironmentAsset);
        ImageAsset* images[]{&candidate.radiance, &candidate.irradiance, &candidate.specular, &candidate.brdfLut};
        for (auto* image : images) {
            uint64_t length = 0;
            std::span<const std::byte> payload;
            if (!reader.U64(length) || length > reader.Remaining() ||
                !reader.Bytes(static_cast<size_t>(length), payload) || resident >= context.maxResidentBytes) {
                error = "Environment image exceeds input or admitted resident budget";
                return false;
            }
            AssetDecodeContext child = context;
            child.maxResidentBytes = context.maxResidentBytes - resident;
            AssetLoadResult decoded;
            if (!ImageAssetLoader::DecodePayload(payload, child, decoded, error))
                return false;
            // 解码器输出const值；深复制时临时双份内存已包含在Image解码的半预算策略中。
            *image = *std::static_pointer_cast<const ImageAsset>(decoded.value);
            resident += image->sourceUri.capacity() + 1 + image->image.pixels.capacity() +
                image->image.subresources.capacity() * sizeof(ImageSubresource);
        }
        if (!reader.AtEnd() || !ValidateEnvironmentAsset(candidate, error) ||
            context.cancellation.IsCancellationRequested()) {
            if (error.empty()) error = "Environment trailing bytes or cancellation";
            return false;
        }
        output = MakeAssetLoadResult<EnvironmentAsset>(std::make_shared<const EnvironmentAsset>(std::move(candidate)), resident);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    AssetCodec EnvironmentAssetLoader::Codec() {
        AssetCodec codec;
        if (!AssetType::TryParse("environment", codec.loader.type))
            throw std::logic_error("Invalid environment asset key");
        codec.loader.cppType = std::type_index(typeid(EnvironmentAsset));
        codec.loader.decode = [](const AssetContainer& container, const AssetDecodeContext& context,
            AssetLoadResult& output, std::string& error) {
            if (container.payloadVersion != 1) { error = "Unsupported environment container version"; return false; }
            return DecodePayload(container.payload, context, output, error);
        };
        codec.encode = [](const AssetLoadResult& value, std::vector<std::byte>& bytes, std::string& error) {
            if (value.cppType != std::type_index(typeid(EnvironmentAsset)) || !value.value) {
                error = "Environment codec type mismatch";
                return false;
            }
            return EncodePayload(*std::static_pointer_cast<const EnvironmentAsset>(value.value), bytes, error);
        };
        return codec;
    }

    bool RegisterEnvironmentAssetLoader(AssetManager& manager, std::string& error) {
        return manager.RegisterLoader(EnvironmentAssetLoader::Codec().loader, error);
    }

}
