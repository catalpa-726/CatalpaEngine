#pragma once

#include "catpch.h"

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"

namespace Catalpa {

    // 渲染侧稳定实例身份，仅为值；不引入 ECS/World 的类型或存储布局。
    struct RenderInstanceId {
        uint64_t scene = 0;
        uint32_t index = 0;
        uint32_t generation = 0;
        [[nodiscard]] constexpr bool HasValue() const noexcept { return scene != 0 && generation != 0 && index != std::numeric_limits<uint32_t>::max(); }
        constexpr auto operator<=>(const RenderInstanceId&) const = default;
    };

    // S03.2 的单视图相机输入；S03.4 才接入离屏目标、多视图和独立历史。
    struct RenderView {
        uint64_t id = 0;
        RenderInstanceId camera;
        uint64_t historyRevision = 0;
        CameraLens lens;
        CameraViewport viewport;
        CameraMatrices matrices;
    };

}
