#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetLoader.h"
#include "Materials/MaterialInstance.h"
namespace Catalpa {
    class AssetManager;
    CATALPA_API size_t MaterialDefinitionBytes(const MaterialDefinition&) noexcept;
    CATALPA_API size_t MaterialInstanceBytes(const MaterialInstance&) noexcept;
    CATALPA_API bool RegisterMaterialAssetLoaders(AssetManager&, std::string& error);
}
