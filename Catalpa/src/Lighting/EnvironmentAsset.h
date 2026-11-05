#pragma once

#include "Images/ImageAsset.h"
#include "Core/Base.h"

namespace Catalpa {

    // 右手Y-up Cube面序+X/-X/+Y/-Y/+Z/-Z；面内s/t从左上像素中心映射到[-1,1]。
    // 面方向依次(1,-t,-s),(-1,-t,s),(s,1,t),(s,-1,-t),(s,-t,1),(-s,-t,-1)。
    // radiance是独立Cube派生；irradiance含cos积分；specular mip对应线性roughness，BRDF LUT仍2D。
    // 原始经纬度HDR保持只读；CPU资产不拥有GPU贴图或采样器。
    struct EnvironmentAsset {
        uint32_t schemaVersion = 1;
        uint32_t preprocessingVersion = 2;
        uint32_t sampleCount = 128;
        ImageAsset radiance, irradiance, specular, brdfLut;
    };

    [[nodiscard]] CATALPA_API bool ValidateEnvironmentAsset(const EnvironmentAsset&, std::string& error);

}
