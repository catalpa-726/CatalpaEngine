#pragma once

#include "RHIShaderDesc.h"
#include "Images/PixelFormat.h"

namespace Catalpa {

    // 当前支持 triangle list、单颜色附件、动态视口/裁剪。
    // Shader 接口暂不包含顶点属性、描述符或 push constants。
    struct RHIGraphicsPipelineDesc {
        RHIShaderDesc vertex{};
        RHIShaderDesc fragment{ .stage = RHIShaderStage::Fragment };
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
    };

}