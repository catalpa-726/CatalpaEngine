#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraph;
    // 首版单定向光shadow map；点/聚光仍参与照明，阴影预算不静默扩张。
    class ShadowPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, std::span<const RGIndexedDrawDesc> draws);
    };
}
