#pragma once

#include "RHITypes.h"
#include "RHIFormat.h"

// 近期资源创建、管线和渲染区域会用到的上限，后续按实际模块扩展
namespace Catalpa {

    // 设备上限与实际启用的能力。
    struct RHICapabilities {
        uint32_t maxTextureDimension2D = 0;
        uint32_t maxTextureArrayLayers = 0;
        uint32_t maxColorAttachments = 0;
        Extent2D maxRenderTargetExtent{};
        uint32_t maxVertexInputAttributes = 0;

        // 以下大小和对齐均以字节为单位。
        uint32_t maxVertexInputAttributeOffset = 0;
        uint32_t maxVertexInputBindingStride = 0;
        uint32_t maxPushConstantBytes = 0;
        uint64_t maxUniformBufferRange = 0;
        uint64_t maxStorageBufferRange = 0;
        uint64_t minUniformBufferOffsetAlignment = 0;
        uint64_t minStorageBufferOffsetAlignment = 0;

        bool compute = false;
        bool drawIndirect = false;
        std::array<uint32_t, 3> maxComputeWorkGroupCount{};
        bool presentFence = false;                      // 是否实际启用呈现 Fence；false 时采用设备空闲兼容回收。
        float maxSamplerLodBias = 0.0f;
    };

}
