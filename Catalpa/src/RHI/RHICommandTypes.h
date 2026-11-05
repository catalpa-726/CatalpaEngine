#pragma once

#include "RHIHandle.h"
#include "RHIResourceAccess.h"

namespace Catalpa {

    enum class RHIIndexType { UInt16, UInt32 };

    struct RHITextureBarrier {
        TextureViewHandle view{};               // view 用来定位图像并保留其寿命
        RHITextureSubresourceRange range{};
        RHITextureAccess before{};
        RHITextureAccess after{};
    };

    struct RHIBufferCopy {
        uint64_t sourceOffset = 0;
        uint64_t destinationOffset = 0;
        uint64_t size = 0;
    };

    // 从 Buffer 上传指定 mip/layer 的整幅紧密二维图像，一次调用处理一个子资源。
    struct RHIBufferTextureCopy {
        uint64_t bufferOffset = 0;
        RHITextureSubresourceRange range{};
    };

    struct RHIBufferBarrier {
        BufferHandle buffer{};
        uint64_t offset = 0;
        uint64_t size = 0;
        RHIBufferAccess before{};
        RHIBufferAccess after{};
    };

    // 将指定子资源的矩形紧密复制到 Buffer，供完成后的异步读回消费。
    struct RHITextureBufferCopy {
        uint64_t bufferOffset = 0;
        RHITextureSubresourceRange range{};
        Offset2D textureOffset{};
        Extent2D extent{};
    };

}
