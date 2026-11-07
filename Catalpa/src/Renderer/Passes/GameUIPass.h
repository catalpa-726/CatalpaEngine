#pragma once
#include "UI/UIDrawData.h"
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "Renderer/RenderGraph/RenderGraphResource.h"
namespace Catalpa {
    class RenderGraph;
    class GameUIPass {
      public:
        GameUIPass(RHIDevice& device, TextureGpuCache& textures)
            : m_Device(device), m_Textures(textures), m_Frames(device.GetFrameCount()) {}
        ~GameUIPass();
        RHIStatus Prepare(uint32_t slot, const UIDrawData&, std::shared_ptr<PipelineResource>);
        RHIStatus Add(RenderGraph&, RGTexture target, Extent2D extent, uint32_t slot);

      private:
        struct Frame {
            ComputeBuffer vertices, indices;
            UIDrawData data;
            std::vector<std::shared_ptr<TextureGpuResource>> textures;
            std::shared_ptr<PipelineResource> pipeline;
        };
        struct TextureEntry {
            std::shared_ptr<const UITextureData> source;
            std::shared_ptr<TextureGpuResource> gpu;
        };
        RHIDevice& m_Device;
        TextureGpuCache& m_Textures;
        std::vector<Frame> m_Frames;
        std::vector<TextureEntry> m_Cache;
        uint64_t m_NextTexture = 1;
        SamplerHandle m_Sampler{};
    };
} // namespace Catalpa
