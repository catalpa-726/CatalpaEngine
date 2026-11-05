#pragma once

#include "Materials/MaterialInstance.h"

namespace Catalpa {

    inline constexpr uint32_t DefaultMaterialSamplerFlags = 240;

    // 领域参数到表面着色的受检值；CPU结构不是GPU块，renderer显式编码五个float4。
    struct MaterialSurfaceParameters {
        std::array<float, 4> baseColor{1, 1, 1, 1};
        std::array<float, 3> emissive{0, 0, 0};
        float metallic = 0, roughness = 0.5f, normalScale = 1, occlusionStrength = 1, alphaCutoff = 0.5f;
        std::array<float, 4> uvScaleOffset{1, 1, 0, 0};
        // 每4096模拟秒的整数频率（可为负）；UV作幅度0.5的正弦摆动，各寻址模式下wrap连续。
        std::array<float, 2> uvCycles{0, 0};
        uint32_t pulseCycles = 0;
        bool flipV = false;
        std::array<AssetHandle<ImageAsset>, 5> textures{}; // base, normal, metalrough, emissive, occlusion。
        // bit0..1 wrapU、bit2..3 wrapV：0 Repeat/1 Clamp/2 Mirror；bit4 minLinear、5 magLinear、6 mipLinear、7 useMip。
        std::array<uint32_t, 5> samplerFlags{240, 240, 240, 240, 240};
    };

    [[nodiscard]] CATALPA_API bool ResolveMaterialSurface(const MaterialInstance&,
        MaterialSurfaceParameters& output, std::string& error);

}
