#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraph;
    // 消费按相机深度稳定排序的Blend清单；深度只读，管线明确选择Alpha或Additive。
    class TransparentPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, std::span<const RGIndexedDrawDesc> draws);
    };
}
