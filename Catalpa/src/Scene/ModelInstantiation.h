#pragma once
#include "Scene/Scene.h"
#include "Geometry/ModelAsset.h"
#include "Materials/MaterialInstance.h"

namespace Catalpa {
    // 显式预览覆盖不改写ModelAsset中的作者材质引用；为空时使用作者的固定版本。
    struct ModelMaterialOverride {
        AssetId source;
        AssetVersionRef<MaterialInstance> value;
    };

    // 仅主线程读写。成功结果在整个模型的单条结构命令提交后发布。
    struct ModelInstantiationResult {
        bool complete = false, success = false;
        bool cancelRequested = false; // 主线程在命令提交前请求取消，整批仍保持零发布。
        std::string error;
        std::vector<EntityReference> roots;
        std::vector<std::pair<StableId, EntityReference>> nodes;
    };

    class CATALPA_API ModelInstantiation final {
    public:
        // 引用均为确切版本；函数不加载资源。任何验证/提交失败回滚本次全部实体。
        static bool Queue(Scene& scene, AssetVersionRef<ModelAsset> model,
                          std::vector<AssetVersionRef<MeshAsset>> meshes,
                          std::vector<AssetVersionRef<MaterialInstance>> materials,
                          std::shared_ptr<ModelInstantiationResult>& output, std::string& error,
                          EntityId parent = {}, std::span<const ModelMaterialOverride> overrides = {});
    };
}
