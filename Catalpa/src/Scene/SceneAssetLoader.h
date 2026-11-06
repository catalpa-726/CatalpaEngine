#pragma once
#include "Assets/AssetLoader.h"
#include "Scene/SceneAsset.h"

namespace Catalpa {
    class AssetManager;
    CATALPA_API bool RegisterSceneAssetLoader(AssetManager&, std::string& error);
}
