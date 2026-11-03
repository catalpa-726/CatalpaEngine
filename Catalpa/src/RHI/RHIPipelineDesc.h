#pragma once

#include "RHIShaderDesc.h"
#include "RHIBindingDesc.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    // StraightAlpha 用于非预乘颜色：RGB = src.rgb * src.a + dst.rgb * (1 - src.a)。
    enum class RHIBlendMode { Opaque, StraightAlpha };

    // 单个从 offset 0 开始的连续参数块，size 单位为字节。
    // size 为 0 时 visibility 必须为 None。
    struct RHIPushConstantDesc {
        uint32_t size = 0;
        RHIShaderVisibility visibility = RHIShaderVisibility::None;
    };

    // 当前支持 triangle list、单颜色附件、动态视口/裁剪和单顶点流。
    // 空顶点布局保留无顶点缓冲绘制；描述符当前限于 set 0 的 fragment 纹理和采样器。
    struct RHIGraphicsPipelineDesc {
        RHIShaderDesc vertex{};
        RHIShaderDesc fragment{ .stage = RHIShaderStage::Fragment };
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
        RHIBlendMode blendMode = RHIBlendMode::Opaque;
        VertexLayout vertexLayout{};
        RHIPushConstantDesc pushConstants{};
        RHIBindingLayoutDesc bindingLayout{};
    };

}
