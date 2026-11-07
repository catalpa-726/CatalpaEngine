#pragma once

#include "Geometry/MeshAsset.h"
#include "Materials/MaterialInstance.h"
#include "ECS/World.h"

namespace Catalpa {

    // CPU几何与固定资产版本；旧CPU构造入口仍可用于数学检查，不拥有GPU资源。
    // MeshResource由受检工厂密封，复制组件只复制保活引用，不复制顶点或索引。
    struct MeshRendererComponent {
        MeshResourceRef mesh;
        Math::Vec4 color{ 1.0f };
        bool visible = true;
        AssetVersionRef<MeshAsset> meshVersion;
        std::vector<AssetVersionRef<MeshAsset>> lods;
        std::vector<float> lodErrors; // 源局部坐标的几何误差，Renderer换算为像素后选择。
        std::vector<AssetVersionRef<MaterialInstance>> materials; // 按mesh材质槽排列，跨帧保活确切版本。

    };

    template<> struct ComponentTraits<MeshRendererComponent> {
        [[nodiscard]] static CATALPA_API const ComponentType& Get();
    };

}
