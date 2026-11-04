#pragma once

#include "Geometry/MeshData.h"
#include "ECS/World.h"

namespace Catalpa {

    // 内置只读几何及实例显示参数；不拥有GPU资源，不依赖Box或未来资产加载器。
    // MeshResource由受检工厂密封，复制组件只复制保活引用，不复制顶点或索引。
    struct MeshRendererComponent {
        MeshResourceRef mesh;
        Math::Vec4 color{ 1.0f };
        bool visible = true;
    };

    template<> struct ComponentTraits<MeshRendererComponent> {
        [[nodiscard]] static CATALPA_API const ComponentType& Get();
    };

}
