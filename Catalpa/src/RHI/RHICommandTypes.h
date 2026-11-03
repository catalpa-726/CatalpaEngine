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

    // 从 Buffer 上传整幅、紧密排列的二维颜色图像。
    // 当前仅支持 mip 0、layer 0；不支持部分区域或自定义行跨度。
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

}