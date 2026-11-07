#pragma once
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/ComputePipelineLibrary.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderFramePacket.h"
namespace Catalpa {
    struct DepthPyramidLevel {
        uint32_t offset = 0, width = 0, height = 0;
    };
    struct GpuSceneFrame {
        ComputeBuffer depthPyramid, lightRects, lightLists, cullInput, cullParameters, indirect;
        std::vector<DepthPyramidLevel> levels;
        std::shared_ptr<ComputePipelineResource> depthKernel, lightKernel, cullKernel;
        uint32_t tilesX = 0, tilesY = 0;
        RGBuffer lightListsGraph;
        uint64_t lightListBytes = 0;
    };
    class GpuScenePasses {
      public:
        static RHIStatus Prepare(RHIDevice&, ComputePipelineLibrary&, const RenderFramePacket&,
                                 const RenderView&, Extent2D, GpuSceneFrame&);
        static RHIStatus AddDepth(RenderGraph&, RGTexture, SamplerHandle, bool reversedZ, GpuSceneFrame&);
        static RHIStatus AddLights(RenderGraph&, uint32_t lights, GpuSceneFrame&);
        static RHIStatus AddVisibility(RHIDevice&, RenderGraph&, const RenderFramePacket&, const RenderView&,
                                       bool useDepth, std::vector<RGIndexedDrawDesc>&, GpuSceneFrame&);
    };
} // namespace Catalpa
