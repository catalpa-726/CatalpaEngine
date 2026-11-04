#pragma once

#include "ECS/World.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

    class Scene;

    // 派生状态更新后复制到自有 packet，失败不发布半帧，不保留组件指针。
    class CATALPA_API RenderExtractSystem {
    public:
        [[nodiscard]] static WorldStatus Extract(const Scene& scene, uint64_t sequence,
            uint32_t width, uint32_t height, RenderFramePacket& output);
    };

}
