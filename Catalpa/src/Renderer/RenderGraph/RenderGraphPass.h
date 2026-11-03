#pragma once

#include "RenderGraphResource.h"
#include "RHI/RHICommandTypes.h"
#include "RHI/RHIRenderingDesc.h"

// 描述当前索引绘制 Pass。复制和绘制都采用明确参数，由同一份参数生成访问声明与命令
namespace Catalpa {

    // 当前支持一个逐顶点数据流、一个索引流和一张完整颜色附件。
    struct RGIndexedDrawDesc {
        RGBuffer vertices{};
        RGBuffer indices{};
        RGTexture color{};
        GraphicsPipelineHandle pipeline{};
        uint64_t vertexOffset = 0;
        uint64_t indexOffset = 0;
        RHIIndexType indexType = RHIIndexType::UInt32;
        uint32_t indexCount = 0;
        uint32_t instanceCount = 1;
        uint32_t firstIndex = 0;
        int32_t baseVertex = 0;
        uint32_t firstInstance = 0;
        RHILoadOp loadOp = RHILoadOp::Clear;
        RHIStoreOp storeOp = RHIStoreOp::Store;
        std::array<float, 4> clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        RHIViewport viewport{};
        Rect2D scissor{};
        std::vector<std::byte> pushConstants;                           // 自有字节副本；空数组表示本次绘制未提供 push constants。
    };

}