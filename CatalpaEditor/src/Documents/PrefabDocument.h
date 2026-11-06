#pragma once
#include "Documents/SceneDocument.h"
#include "Scene/PrefabAsset.h"

namespace Catalpa::Editor {
    // Prefab 文件复用 AssetDocument 的源文档/历史；此适配器只产生可复用的作者子树。
    struct PrefabDocument {
        static bool FromSelection(SceneDocument&, PersistentEntityId root, PrefabAsset&, std::string& error);
    };
}
