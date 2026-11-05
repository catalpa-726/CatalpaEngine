#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RenderGraph;
    // 线性HDR场景应用曝光和ACES；输出仍为线性，SRGB附件负责唯一一次输出编码。
    class TonemapPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc, float exposureEV);
    };
}
