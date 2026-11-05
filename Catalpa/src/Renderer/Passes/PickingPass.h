#pragma once

#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraph;
    // R32_UInt输出一基实例token；Mask共用cutoff，Blend使用统一的0.1 alpha阈值并写深度。
    class PickingPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, RGTexture ids,
            std::span<const RGIndexedDrawDesc> draws, float clearDepth);
    };
}
