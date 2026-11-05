#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RenderGraph;
    // HDR背景在不透明场景之前写入，后续深度不受天空影响。
    class SkyboxPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
    };
}
