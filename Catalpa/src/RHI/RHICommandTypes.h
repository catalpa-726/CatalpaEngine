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

    struct RHIBufferBarrier {
        BufferHandle buffer{};
        uint64_t offset = 0;
        uint64_t size = 0;
        RHIBufferAccess before{};
        RHIBufferAccess after{};
    };

}