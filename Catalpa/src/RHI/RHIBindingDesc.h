#pragma once

#include "RHIHandle.h"
#include "RHIShaderDesc.h"

namespace Catalpa {

    inline constexpr uint32_t RHIMaxBindingsPerSet = 16;

    enum class RHIBindingType { SampledTexture, Sampler };

    // 当前只支持 set 0，每个 binding 对应一个 fragment 阶段描述符。
    struct RHIBindingLayoutEntry {
        uint32_t binding = 0;
        RHIBindingType type = RHIBindingType::SampledTexture;
        RHIShaderVisibility visibility = RHIShaderVisibility::Fragment;
    };

    // 创建管线时同步复制 entries，调用结束后不再借用此 span。
    struct RHIBindingLayoutDesc {
        std::span<const RHIBindingLayoutEntry> entries{};
    };

    // 只填写 type 对应的句柄，另一个句柄保持为空。
    struct RHIBinding {
        uint32_t binding = 0;
        RHIBindingType type = RHIBindingType::SampledTexture;
        TextureViewHandle texture{};
        SamplerHandle sampler{};
    };

}
