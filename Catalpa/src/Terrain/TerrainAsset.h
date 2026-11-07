#pragma once
#include "catpch.h"
#include "Assets/AssetContainerFormat.h"
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
#include "Materials/MaterialInstance.h"
#include "Physics/CollisionShapeAsset.h"
#include "Navigation/NavMeshAsset.h"

namespace Catalpa {
    struct TerrainSettings {
        uint32_t width = 65, height = 65, tileQuads = 32, lodCount = 4;
        float spacing = 1, heightScale = 1, heightOffset = 0, skirtDepth = 32;
        bool buildCollision = true, buildNavigation = false;
    };
    struct TerrainSource {
        std::string heightSource, encoding = "r16_unorm_le";
        TerrainSettings settings;
        AssetId material;
    };
    struct TerrainTileRecord {
        uint32_t x = 0, z = 0, lod = 0;
        AssetDependency mesh, collision;
        Aabb bounds;
    };
    struct TerrainAsset {
        TerrainSettings settings;
        std::string heightDigest;
        AssetDependency material, navigation;
        std::vector<TerrainTileRecord> tiles;
        // 固定VFS与目录版本，不借用AssetManager。tile worker仅从此快照读取。
        std::function<bool(AssetId, size_t, std::vector<std::byte>&, std::string&)> readDeferred;
    };
    struct TerrainHeightData {
        TerrainSettings settings;
        std::vector<float> heights;
    };
    CATALPA_API bool ValidateTerrainSettings(const TerrainSettings&, std::string&);
    CATALPA_API bool ValidateTerrainHeights(const TerrainHeightData&, std::string&);
    CATALPA_API bool ReadTerrainSource(std::string_view, TerrainSource&, std::string&);
    CATALPA_API bool WriteTerrainSource(const TerrainSource&, std::string&, std::string&);
    CATALPA_API bool ReadTerrainAsset(std::string_view, TerrainAsset&, std::string&);
    CATALPA_API bool WriteTerrainAsset(const TerrainAsset&, std::string&, std::string&);
    CATALPA_API bool BuildTerrainTile(const TerrainHeightData&, uint32_t x, uint32_t z, uint32_t lod,
                                      MeshAsset&, std::string&);
    CATALPA_API bool BuildTerrainCollision(const TerrainHeightData&, uint32_t x, uint32_t z,
                                           CollisionShapeAsset&, std::string&);
    class AssetManager;
    CATALPA_API bool RegisterTerrainAssetLoader(AssetManager&, std::string&);
} // namespace Catalpa
