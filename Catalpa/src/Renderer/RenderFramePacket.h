#pragma once

#include "catpch.h"

#include "Renderer/RenderView.h"
#include "Geometry/MeshData.h"

namespace Catalpa {

    // 场景最终世界变换值；几何实例通过相同身份关联，渲染方不借用组件内存。
    struct RenderTransform {
        RenderInstanceId id;
        Math::Mat4 worldMatrix{ 1.0f };
    };

    // 已派生的只读几何实例；world为列向量矩阵，normal为inverse(transpose)线性变换。
    // 多实例保活同一密封mesh，世界bounds用于未来剔除，不持有GPU对象。
    struct RenderMeshInstance {
        RenderInstanceId id;
        MeshResourceRef mesh;
        Math::Mat4 worldMatrix{ 1.0f };
        Math::Mat3 normalMatrix{ 1.0f };
        Aabb worldBounds;
        Math::Vec4 color{ 1.0f };
    };

    // 单线程提取后向渲染方转移所有权，任何字段都不借用可变 World 内存。
    struct RenderFramePacket {
        uint64_t sequence = 0;
        uint64_t sceneIdentity = 0;
        std::vector<RenderTransform> transforms;
        std::vector<RenderView> views;
        std::vector<RenderMeshInstance> instances;
    };

}
