#include "catpch.h"
#include "AssetPipeline/Cookers/EnvironmentCooker.h"

namespace Catalpa::Tools {

    bool EnvironmentCooker::Encode(const EnvironmentAsset& asset, std::vector<std::byte>& bytes,
        std::string& error) {
        return EnvironmentAssetLoader::EncodePayload(asset, bytes, error);
    }

}
