#pragma once

// 说明在哪个执行阶段、怎样访问图像，以及使用什么布局
namespace Catalpa {

    enum class RHIPipelineStage { None, ColorAttachmentOutput, AllCommands };
    enum class RHIResourceAccess { None, ColorRead, ColorWrite, ColorReadWrite };
    enum class RHITextureLayout { Undefined, ColorAttachment, Present };

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

}