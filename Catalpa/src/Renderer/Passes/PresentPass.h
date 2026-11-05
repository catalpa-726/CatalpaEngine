#pragma once
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RenderGraph;
    // 已tonemap的线性显示图传到宿主SRGB附件；UI在此之后合成，不受场景曝光污染。
    class PresentPass {
    public:
        [[nodiscard]] static RHIStatus Add(RenderGraph&, RGIndexedDrawDesc);
    };
}
