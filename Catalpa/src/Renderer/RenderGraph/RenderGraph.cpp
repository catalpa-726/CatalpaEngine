#include "catpch.h"
#include "RenderGraph.h"

namespace Catalpa {
    RenderGraph::RenderGraph(RHIDevice& device) : RenderGraphBuilder(device) {}

    RHIStatus RenderGraph::Compile(RenderGraphPlan& plan)
    {
        return RenderGraphCompiler::Compile(*this, plan);
    }
}
