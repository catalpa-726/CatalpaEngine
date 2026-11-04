#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetCodec.h"
#include "Images/ImageAsset.h"
namespace Catalpa {
    class AssetManager;
    class CATALPA_API ImageAssetLoader {
    public:
        static bool EncodePayload(const ImageAsset&, std::vector<std::byte>&, std::string& error);
        static bool DecodePayload(std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
        // Shared typed encoder/decoder contract for cooked tools and runtime registration.
        static AssetCodec Codec();
    };
    CATALPA_API bool RegisterImageAssetLoader(AssetManager&, std::string& error);
}
