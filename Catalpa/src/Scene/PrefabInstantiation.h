#pragma once
#include "Scene/PrefabAsset.h"
#include "Scene/SceneSubtree.h"

namespace Catalpa {
    class Scene;
    class AssetManager;
    // resolver 返回固定来源版本，递归过程中不能切换到不同版本。
    using PrefabResolver = std::function<bool(AssetId, std::shared_ptr<const PrefabAsset>&, uint64_t&, std::string&)>;
    class CATALPA_API PrefabInstantiation {
      public:
        static bool Instantiate(Scene&, AssetId, uint64_t, const PrefabAsset&, AssetManager*,
                                const PrefabResolver&, PersistentEntityId parent,
                                PersistentEntityId& root, std::string& error);
        // 更新沿用旧映射中的身份；调用方负责用文档命令捕获被替换子树。
        static bool Prepare(Scene&, AssetId, uint64_t, const PrefabAsset&, AssetManager*,
                            const PrefabResolver&, PersistentEntityId parent,
                            const std::map<PersistentEntityId, PersistentEntityId>& reuse,
                            SceneSubtreeSnapshot&, PersistentEntityId& root, std::string& error);
    };
}
