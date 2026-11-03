#pragma once

#include "RHITypes.h"
#include "Images/PixelFormat.h"

// 近期资源创建、管线和渲染区域会用到的上限，后续按实际模块扩展
namespace Catalpa {

    // 设备上限与实际启用的能力。
    struct RHICapabilities {
        uint32_t maxTextureDimension2D = 0;
        uint32_t maxTextureArrayLayers = 0;
        uint32_t maxColorAttachments = 0;
        Extent2D maxRenderTargetExtent{};

        // 以下大小和对齐均以字节为单位。
        uint32_t maxPushConstantBytes = 0;
        uint64_t maxUniformBufferRange = 0;
        uint64_t maxStorageBufferRange = 0;
        uint64_t minUniformBufferOffsetAlignment = 0;
        uint64_t minStorageBufferOffsetAlignment = 0;

        bool presentFence = false;                      // 是否实际启用呈现 Fence；false 时采用设备空闲兼容回收。
    };

    // 普通 GPU 纹理的单项格式能力，不用于线性布局图像或纹素缓冲。
    struct RHIFormatSupport {
        bool sampledImage = false;
        bool linearFiltering = false;
        bool storageImage = false;
        bool colorAttachment = false;
        bool colorAttachmentBlend = false;
        bool transferSource = false;
        bool transferDestination = false;
    };

}