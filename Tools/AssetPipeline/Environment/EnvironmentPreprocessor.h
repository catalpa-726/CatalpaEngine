#pragma once

#include "Lighting/EnvironmentAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {

    struct EnvironmentPreprocessSettings {
        uint32_t radianceFaceSize = 256;
        uint32_t irradianceFaceSize = 16;
        uint32_t specularFaceSize = 64;
        uint32_t brdfSize = 64;
        uint32_t sampleCount = 128;
        size_t maxWorkingBytes = 256 * 1024 * 1024;
    };

    // 只从线性RGBA32F经纬度源派生6-face Cube IBL，不写原件；Hammersley积分无GPU依赖。
    class EnvironmentPreprocessor final {
    public:
        static bool Build(const ImageAsset& source, const EnvironmentPreprocessSettings&,
            CancellationToken, EnvironmentAsset& output, std::string& error);
    };

}
