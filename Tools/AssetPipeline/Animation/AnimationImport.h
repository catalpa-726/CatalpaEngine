#pragma once
#include "AssetPipeline/AssetBuildContext.h"
struct aiScene;
namespace fastgltf {
    struct Asset;
}

namespace Catalpa::Tools {
    using PrimitiveRemaps = std::map<std::pair<size_t, size_t>, std::vector<uint32_t>>;
    bool AppendAssimpAnimations(AssetBuildContext&, const aiScene&, ModelAsset&, AssetImportResult&,
                                const std::vector<std::vector<uint32_t>>&, std::string& error);
    bool AppendGltfAnimations(AssetBuildContext&, const fastgltf::Asset&, ModelAsset&, AssetImportResult&,
                              const PrimitiveRemaps&, std::string& error);
} // namespace Catalpa::Tools
