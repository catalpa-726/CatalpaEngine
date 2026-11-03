#pragma once

// 描述资源的执行阶段和访问方式；纹理另有布局与子资源范围。
namespace Catalpa {

    enum class RHIPipelineStage { None, ColorAttachmentOutput, AllCommands, Transfer, VertexInput };
    enum class RHIResourceAccess {
        None, ColorRead, ColorWrite, ColorReadWrite,
        TransferRead, TransferWrite, VertexRead, IndexRead, VertexIndexRead,
        TransferReadWrite, MemoryReadWrite
    };
    enum class RHITextureLayout { Undefined, ColorAttachment, Present, TransferDestination };

    struct RHITextureAccess {
        RHIPipelineStage stage = RHIPipelineStage::None;
        RHIResourceAccess access = RHIResourceAccess::None;
        RHITextureLayout layout = RHITextureLayout::Undefined;
    };

    // 范围按底层图像编址；本批只支持颜色图像的 mip 0、layer 0。
    struct RHITextureSubresourceRange {
        uint32_t baseMipLevel = 0;
        uint32_t levelCount = 1;
        uint32_t baseArrayLayer = 0;
        uint32_t layerCount = 1;

        constexpr bool operator==(const RHITextureSubresourceRange&) const = default;
    };

    struct RHIBufferAccess {
        RHIPipelineStage stage = RHIPipelineStage::None;
        RHIResourceAccess access = RHIResourceAccess::None;
    };

}