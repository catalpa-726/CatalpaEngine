#pragma once

#include "RHIShaderDesc.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    // 单个从 offset 0 开始的连续参数块，size 单位为字节。
    // size 为 0 时 visibility 必须为 None。
    struct RHIPushConstantDesc {
        uint32_t size = 0;
        RHIShaderVisibility visibility = RHIShaderVisibility::None;
    };

    // 当前支持 triangle list、单颜色附件、动态视口/裁剪和单顶点流。
    // 空顶点布局保留无顶点缓冲绘制；暂不包含描述符。
    struct RHIGraphicsPipelineDesc {
        RHIShaderDesc vertex{};
        RHIShaderDesc fragment{ .stage = RHIShaderStage::Fragment };
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
        VertexLayout vertexLayout{};
        RHIPushConstantDesc pushConstants{};
    };

}