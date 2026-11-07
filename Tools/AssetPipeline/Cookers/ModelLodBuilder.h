#pragma once
#include "AssetPipeline/AssetBuildContext.h"
namespace Catalpa::Tools {
    // 不覆盖 LOD0 或来源文件；额外产物按材质槽分别简化，保留拓扑边界和 UV/法线接缝。
    bool BuildModelLods(AssetBuildContext&, ModelAsset&, AssetImportResult&, std::string& error);
    bool SimplifyMeshLod(const MeshAsset&, float triangleRatio, float relativeError, MeshAsset&,
                         float& absoluteError, std::string& error);
} // namespace Catalpa::Tools
