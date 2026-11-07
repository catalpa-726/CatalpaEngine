#pragma once

// 描述资源的执行阶段和访问方式；纹理另有布局与子资源范围。
namespace Catalpa {

    enum class RHIPipelineStage {
        None,
        ColorAttachmentOutput,
        AllCommands,
        Transfer,
        VertexInput,
        FragmentShader,
        VertexShader,
        VertexFragmentShader,
        DepthStencilTests,
        Host,
        ComputeShader,
        DrawIndirect
    };
    enum class RHIResourceAccess {
        None,
        ColorRead,
        ColorWrite,
        ColorReadWrite,
        TransferRead,
        TransferWrite,
        VertexRead,
        IndexRead,
        VertexIndexRead,
        TransferReadWrite,
        MemoryReadWrite,
        SampledRead,
        UniformRead,
        DepthRead,
        DepthWrite,
        DepthReadWrite,
        HostRead,
        StorageRead,
        StorageWrite,
        StorageReadWrite,
        IndirectRead
    };
    enum class RHITextureLayout { Undefined, ColorAttachment, Present, TransferDestination, ShaderReadOnly, DepthAttachment, TransferSource };

    struct RHITextureAccess {
        RHIPipelineStage stage = RHIPipelineStage::None;
        RHIResourceAccess access = RHIResourceAccess::None;
        RHITextureLayout layout = RHITextureLayout::Undefined;
    };

    enum class RHITextureAspect { Color, Depth };

    // mip 和 layer 为图像中的绝对索引；同一图像的布局转换保守覆盖完整视图。
    struct RHITextureSubresourceRange {
        uint32_t baseMipLevel = 0;
        uint32_t levelCount = 1;
        uint32_t baseArrayLayer = 0;
        uint32_t layerCount = 1;

        RHITextureAspect aspect = RHITextureAspect::Color;

        constexpr bool operator==(const RHITextureSubresourceRange&) const = default;
    };

    struct RHIBufferAccess {
        RHIPipelineStage stage = RHIPipelineStage::None;
        RHIResourceAccess access = RHIResourceAccess::None;
    };

}
