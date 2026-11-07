#pragma once

#include "RHIShaderDesc.h"
#include "RHIBindingDesc.h"
#include "Images/PixelFormat.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    // StraightAlpha 用于非预乘颜色：RGB = src.rgb * src.a + dst.rgb * (1 - src.a)。
    // Additive 的 RGB 使用 src.rgb * src.a + dst.rgb；alpha 保持普通覆盖率合成。
    enum class RHIBlendMode { Opaque, StraightAlpha, Additive, Premultiplied };

    // 单个从 offset 0 开始的连续参数块，size 单位为字节。
    // size 为 0 时 visibility 必须为 None。
    struct RHIPushConstantDesc {
        uint32_t size = 0;
        RHIShaderVisibility visibility = RHIShaderVisibility::None;
    };

    enum class RHICompareOp { Always, Less, LessEqual, Equal, Greater, GreaterEqual };
    enum class RHICullMode { None, Back, Front };
    enum class RHIFrontFace { CounterClockwise, Clockwise };
    enum class RHITopology { TriangleList, LineList };
    struct RHIDepthState {
        bool testEnabled = false;
        bool writeEnabled = false;
        RHICompareOp compareOp = RHICompareOp::Less;
    };

    // 当前支持triangle list、至多一个颜色附件与可选D32深度、动态视口/裁剪及单顶点流。
    // 空顶点布局可无顶点缓冲；set0支持vertex/fragment UBO和fragment纹理/sampler。
    struct RHIGraphicsPipelineDesc {
        RHIShaderDesc vertex{};
        RHIShaderDesc fragment{ .stage = RHIShaderStage::Fragment };
        PixelFormat colorFormat = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
        RHIBlendMode blendMode = RHIBlendMode::Opaque;
        VertexLayout vertexLayout{};
        RHIPushConstantDesc pushConstants{};
        RHIBindingLayoutDesc bindingLayout{};
        PixelFormat depthFormat = PixelFormat::Unknown;
        RHIDepthState depthState{};
        RHICullMode cullMode = RHICullMode::None;
        RHIFrontFace frontFace = RHIFrontFace::CounterClockwise;
        RHITopology topology = RHITopology::TriangleList;
        float depthBiasConstant = 0.0f;
        float depthBiasSlope = 0.0f;
    };

    struct RHIComputePipelineDesc {
        RHIShaderDesc shader{.stage = RHIShaderStage::Compute};
        RHIPushConstantDesc pushConstants{};
        RHIBindingLayoutDesc bindingLayout{};
    };
    [[nodiscard]] CATALPA_API bool ValidateRHIComputeShaderInterface(const RHIComputePipelineDesc&,
                                                                     std::string& error);

    // 反射接口须与显式GPU布局吻合后才允许交后端。
    [[nodiscard]] CATALPA_API bool ValidateRHIPipelineShaderInterface(const RHIGraphicsPipelineDesc&, std::string& error);
}
