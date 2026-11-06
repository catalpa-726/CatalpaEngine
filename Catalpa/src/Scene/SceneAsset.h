#pragma once
#include "Scene/Components/IdentityComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Scene/Components/ScriptComponent.h"
#include "Scene/Components/BehaviorGraphComponent.h"
#include "Scene/Components/PrefabInstanceComponent.h"

namespace Catalpa {
    struct SceneMeshAsset {
        AssetId mesh;
        std::vector<AssetId> materials;
        MeshResourceRef procedural;
        Math::Vec4 color{1};
        bool visible = true;
        // 内存内克隆保活已经显示的版本；写文件时只保存稳定身份。
        std::optional<MeshRendererComponent> pinned;
    };

    struct SceneEntityAsset {
        PersistentEntityId id, parent;
        std::string name;
        Math::TransformTRS local;
        std::optional<CameraComponent> camera;
        std::optional<SceneMeshAsset> mesh;
        std::optional<LightComponent> light;
        std::optional<EnvironmentComponent> environment;
        std::optional<ScriptComponent> script;
        std::optional<BehaviorGraphComponent> behavior;
        std::optional<PrefabInstanceComponent> prefab;
    };

    // 作者值和稳定引用，不含 World 槽位、系统回调或 GPU 对象。
    struct SceneAsset {
        std::string name;
        std::vector<SceneEntityAsset> entities;
    };
}
