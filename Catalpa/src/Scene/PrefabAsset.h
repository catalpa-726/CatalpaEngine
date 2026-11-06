#pragma once
#include "Scene/SceneAsset.h"

namespace Catalpa {
    struct PrefabNest {
        StableId id;
        AssetId asset;
        PersistentEntityId parent;
        Math::TransformTRS local;
    };
    struct PrefabAsset {
        SceneAsset scene;
        std::vector<PrefabNest> nested;
        // Loader 绑定该 cooked 版本声明的依赖；嵌套展开不能跳到稍后发布的 current。
        std::map<AssetId, AssetVersionRef<PrefabAsset>> nestedVersions;
    };
    CATALPA_API bool ReadPrefab(std::string_view text, PrefabAsset&, std::string& error);
    CATALPA_API bool WritePrefab(const PrefabAsset&, std::string&, std::string& error);
    CATALPA_API bool RegisterPrefabAssetLoader(class AssetManager&, std::string& error);
}
