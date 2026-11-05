#pragma once
#include "Geometry/MeshAsset.h"
#include "Materials/MaterialInstance.h"

#include "catpch.h"

#include "Renderer/RenderView.h"
#include "Geometry/MeshData.h"
#include "Core/Time/FrameTime.h"
#include "Lighting/LightParameters.h"
#include "Lighting/EnvironmentAsset.h"
#include "Lighting/EnvironmentSettings.h"

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
        AssetVersionRef<MeshAsset> meshVersion;
        std::vector<AssetVersionRef<MaterialInstance>> materials;
    };

    // 光源方向为实体局部 -Z 经世界线性变换后的单位向量；不借用组件或设备对象。
    struct RenderLight {
        RenderInstanceId id;
        LightParameters parameters;
        Math::Vec3 position{};
        Math::Vec3 direction{ 0.0f, 0.0f, -1.0f };
    };

    struct RenderEnvironment {
        AssetHandle<EnvironmentAsset> asset;
        EnvironmentSettings settings;
        AssetVersionRef<EnvironmentAsset> version;
    };

    // 单线程提取后向渲染方转移所有权，任何字段都不借用可变 World 内存。
    struct RenderFramePacket {
        uint64_t sequence = 0;
        uint64_t sceneIdentity = 0;
        std::vector<RenderTransform> transforms;
        std::vector<RenderView> views;
        std::vector<RenderMeshInstance> instances;
        // Runtime在场景提取完成后复制本展示帧时间，GPU参数不再借用时钟或World。
        FrameTime time;
        std::vector<RenderLight> lights;
        std::optional<RenderEnvironment> environment;
    };

}
