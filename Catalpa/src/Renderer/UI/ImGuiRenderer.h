#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include "RHI/RHIHandle.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "RHI/RHIError.h"
#include "RHI/RHIBufferDesc.h"
#include "Images/PixelFormat.h"
#include "Renderer/RenderGraph/RenderGraphResource.h"

namespace Catalpa {

    class RHIDevice;
    class AssetManager;
    class RenderGraph;
    class UploadScheduler;
    struct ImGuiFrameData;

    // 主窗口 UI 的 RHI 桥；只消费自有 CPU 数据，不访问 ImGui 上下文或原生 Vulkan。
    class ImGuiRenderer {
    public:
        ImGuiRenderer(RHIDevice& device, AssetManager&, TextureGpuCache&, PipelineLibrary&);
        ~ImGuiRenderer();
        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

        // 固定英文 atlas，立即复制像素到现有上传器；首帧图内完成上传。
        [[nodiscard]] RHIStatus InitFont(const AssetVersionRef<ImageAsset>&);
        // 仅在 PrepareFrame 返回 ready 后调用，安全覆盖该槽的 host-visible 缓冲。
        [[nodiscard]] RHIStatus Prepare(uint32_t slot, const ImGuiFrameData& frame, PixelFormat format);
        // 在场景之后、Present 之前按 UI 顺序登记 Load/Store 绘制及采样访问。
        [[nodiscard]] RHIStatus AddPasses(RenderGraph& graph, RGTexture color, Extent2D extent,
            uint32_t slot, const ImGuiFrameData& frame);

    private:
        struct FrameBuffers {
            BufferHandle vertices{}, indices{};
            uint64_t vertexCapacity = 0, indexCapacity = 0;
        };

        [[nodiscard]] RHIStatus EnsurePipeline(PixelFormat format);
        // 新容量恰好等于这次数据长度，首次全量写入满足图的初始化范围约束。
        [[nodiscard]] RHIStatus WriteDynamicBuffer(BufferHandle& buffer, uint64_t& capacity,
            RHIBufferUsage usage, std::span<const std::byte> bytes);

        RHIDevice& m_Device;
        std::vector<FrameBuffers> m_Frames;
        AssetManager& m_Assets;
        TextureGpuCache& m_Textures;
        PipelineLibrary& m_Pipelines;
        std::shared_ptr<TextureGpuResource> m_Font;
        std::shared_ptr<PipelineResource> m_Pipeline;
        SamplerHandle m_Sampler{};
        PixelFormat m_Format = PixelFormat::Unknown;
    };

}
