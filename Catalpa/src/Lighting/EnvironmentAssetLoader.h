#pragma once

#include "Lighting/EnvironmentAsset.h"
#include "Assets/AssetCodec.h"

namespace Catalpa {

    class AssetManager;
    class CATALPA_API EnvironmentAssetLoader {
    public:
        static bool EncodePayload(const EnvironmentAsset&, std::vector<std::byte>&, std::string& error);
        static bool DecodePayload(std::span<const std::byte>, const AssetDecodeContext&,
            AssetLoadResult&, std::string& error);
        static AssetCodec Codec();
    };
    CATALPA_API bool RegisterEnvironmentAssetLoader(AssetManager&, std::string& error);

}
