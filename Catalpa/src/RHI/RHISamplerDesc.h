#pragma once

namespace Catalpa {

    enum class RHIFilter { Nearest, Linear };
    enum class RHISamplerAddressMode { Repeat, ClampToEdge };

    // 当前为二维颜色纹理采样，固定访问 mip 0。
    struct RHISamplerDesc {
        RHIFilter minFilter = RHIFilter::Nearest;
        RHIFilter magFilter = RHIFilter::Nearest;
        RHISamplerAddressMode addressU = RHISamplerAddressMode::Repeat;
        RHISamplerAddressMode addressV = RHISamplerAddressMode::Repeat;
    };

}
