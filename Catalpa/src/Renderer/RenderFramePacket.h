#pragma once

#include "catpch.h"

#include "Renderer/RenderView.h"

namespace Catalpa {

    // 此阶段只复制最终世界变换；几何、材质与资源版本在对应后续子步接入。
    struct RenderTransform {
        RenderInstanceId id;
        Math::Mat4 worldMatrix{ 1.0f };
    };

    // 单线程提取后向渲染方转移所有权，任何字段都不借用可变 World 内存。
    struct RenderFramePacket {
        uint64_t sequence = 0;
        uint64_t sceneIdentity = 0;
        std::vector<RenderTransform> transforms;
        std::vector<RenderView> views;
    };

}
