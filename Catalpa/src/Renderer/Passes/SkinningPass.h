#pragma once
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/ComputePipelineLibrary.h"
#include "Renderer/RenderFramePacket.h"
namespace Catalpa {
    class RenderGraph;
    struct SkinningFrame {
        ComputeBuffer weights, palette, output, status, readback;
        bool reportedFailure = false;
        AssetVersionRef<MeshSkinBinding> binding;
        std::shared_ptr<ComputePipelineResource> pipeline;
    };
    class SkinningPass {
      public:
        static RHIStatus Prepare(RHIDevice&, ComputePipelineLibrary&, const RenderSkinningInput&,
                                 SkinningFrame&);
        static RHIStatus Add(RenderGraph&, BufferHandle source, uint32_t count, SkinningFrame&);
    };
} // namespace Catalpa
