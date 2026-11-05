#include "catpch.h"
#include "AssetPipeline/Cookers/MeshCooker.h"

namespace Catalpa::Tools {
    bool MeshCooker::Encode(const MeshAsset& v, std::vector<std::byte>& b, std::string& e) {
        return GeometryAssetLoader::EncodeMesh(v, b, e);
    }
}
