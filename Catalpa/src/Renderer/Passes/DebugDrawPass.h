#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RenderGraph;
    // 深度只读的世界线条叠加；使用真实LineList绘制，Renderer拥有动态帧缓冲。
    class DebugDrawPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
    };
}
