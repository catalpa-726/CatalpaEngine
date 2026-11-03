#pragma once

#include "RHIHandle.h"
#include "RHIResourceAccess.h"

namespace Catalpa {

    struct RHITextureBarrier {
        TextureViewHandle view{};               // view 用来定位图像并保留其寿命
        RHITextureSubresourceRange range{};
        RHITextureAccess before{};
        RHITextureAccess after{};
    };

}