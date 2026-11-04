#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetLoader.h"
#include "Geometry/MeshAsset.h"
#include "Geometry/ModelAsset.h"
namespace Catalpa {
    class AssetManager;
    class CATALPA_API GeometryAssetLoader {
    public:
        static bool EncodeMesh(const MeshAsset&, std::vector<std::byte>&, std::string& error);
        static bool EncodeModel(const ModelAsset&, std::vector<std::byte>&, std::string& error);
        static bool DecodeMesh(std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
        static bool DecodeModel(std::span<const std::byte>, const AssetDecodeContext&, AssetLoadResult&, std::string& error);
    };
    CATALPA_API bool RegisterGeometryAssetLoaders(AssetManager&, std::string& error);
}
