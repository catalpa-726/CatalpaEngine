#pragma once

#include "RHIShaderDesc.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    // 当前支持 triangle list、单颜色附件、动态视口/裁剪和单顶点流。
    // 空顶点布局保留无顶点缓冲绘制；暂不包含描述符或 push constants。
    struct RHIGraphicsPipelineDesc {
        RHIShaderDesc vertex{};
        RHIShaderDesc fragment{ .stage = RHIShaderStage::Fragment };
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
        VertexLayout vertexLayout{};
    };

}