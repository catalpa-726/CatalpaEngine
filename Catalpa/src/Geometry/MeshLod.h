#pragma once
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
namespace Catalpa {
    CATALPA_API bool ValidateMeshLodChain(const AssetVersionRef<MeshAsset>& base,
                                          std::span<const AssetVersionRef<MeshAsset>> levels,
                                          std::span<const float> errors, std::string&);
}
