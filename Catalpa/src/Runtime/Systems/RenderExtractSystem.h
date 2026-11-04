#pragma once

#include "ECS/World.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

    class Scene;

    // 一个输出view的CPU请求；cameraViewId=0沿用优先级选择，否则只在对应相机组中选择。
    // id是输出身份，允许同一相机服务不同尺寸的view；目标代次由目标拥有者提供，不是GPU句柄。
    struct RenderViewRequest {
        uint64_t id = 1;
        uint64_t cameraViewId = 0;
        uint64_t targetGeneration = 1;
        CameraViewport viewport;
    };

    // 派生状态更新后复制到自有 packet，失败不发布半帧，不保留组件指针。
    class CATALPA_API RenderExtractSystem {
    public:
        [[nodiscard]] static WorldStatus Extract(const Scene& scene, uint64_t sequence,
            uint32_t width, uint32_t height, RenderFramePacket& output);
        // 多view共享一次场景提取，分别构建投影；零尺寸跳过该view，空请求明确不生成view。
        // 非零尺寸的显式相机缺失、重复输出身份或任一失败均保留原output。
        [[nodiscard]] static WorldStatus Extract(const Scene& scene, uint64_t sequence,
            std::span<const RenderViewRequest> requests, RenderFramePacket& output);
    };

}
