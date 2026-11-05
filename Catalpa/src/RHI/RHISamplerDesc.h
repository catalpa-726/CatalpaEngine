#pragma once

#include "RHIPipelineDesc.h"

namespace Catalpa {

    enum class RHIFilter { Nearest, Linear };
    enum class RHISamplerAddressMode { Repeat, ClampToEdge, MirroredRepeat };

    // LOD 以 mip 为单位；比较采样用于深度纹理，不隐式改变投影深度方向。
    struct RHISamplerDesc {
        RHIFilter minFilter = RHIFilter::Nearest;
        RHIFilter magFilter = RHIFilter::Nearest;
        RHIFilter mipFilter = RHIFilter::Linear;
        RHISamplerAddressMode addressU = RHISamplerAddressMode::Repeat;
        RHISamplerAddressMode addressV = RHISamplerAddressMode::Repeat;
        RHISamplerAddressMode addressW = RHISamplerAddressMode::Repeat;
        float minLod = 0.0f;
        float maxLod = 0.0f;
        float mipLodBias = 0.0f;
        bool compareEnabled = false;
        RHICompareOp compareOp = RHICompareOp::LessEqual;
    };

}
