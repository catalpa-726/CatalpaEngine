#pragma once
#include "Assets/AssetId.h"
#include "Scene/PersistentEntityId.h"

namespace Catalpa {
    // 仅交付自有值；编辑器不能经由调试快照访问运行中的 World 或行为实例。
    struct BehaviorDebugSnapshot {
        PersistentEntityId entity;
        StableId node;
        std::string event, error;
        std::map<std::string, double, std::less<>> variables;
        AssetId graph;
        uint64_t version = 0;
    };
}
