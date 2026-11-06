#pragma once
#include "Scene/PrefabInstantiation.h"

namespace Catalpa {
    class CATALPA_API PrefabReconciler {
      public:
        static bool DescribeOverrides(const Scene&, PersistentEntityId root, std::vector<std::string>&,
                                      std::string& error);
        // 先准备并校验完整候选；支持名字、局部 TRS、实例颜色/可见性覆盖。
        // 其他组件或结构冲突明确拒绝，不把无法表达的修改静默重置。
        static bool Prepare(Scene&, PersistentEntityId root, uint64_t version, const PrefabAsset&,
                            AssetManager*, const PrefabResolver&, bool revertOverrides,
                            SceneSubtreeSnapshot& candidate, std::string& error);
        static bool Replace(Scene&, PersistentEntityId root, const SceneSubtreeSnapshot& candidate,
                            SceneSubtreeSnapshot& previous, const SceneSubtreeSnapshot* expected,
                            std::string& error);
    };
}
