#include "catpch.h"
#include "AssetPipeline/Cookers/TextureCooker.h"

namespace Catalpa::Tools {
    bool TextureCooker::Encode(const ImageAsset& v, std::vector<std::byte>& b, std::string& e) {
        return ImageAssetLoader::EncodePayload(v, b, e);
    }
}
