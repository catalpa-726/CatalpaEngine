#include "catpch.h"
#include "Renderer/Passes/ShadowPass.h"
#include "Renderer/Passes/DepthPrepass.h"

namespace Catalpa {
    RHIStatus ShadowPass::Add(RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws)
    {
        return DepthPrepass::Add(graph, draws, 1.0f);
    }
}
