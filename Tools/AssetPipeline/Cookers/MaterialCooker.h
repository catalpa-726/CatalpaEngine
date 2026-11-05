#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 同时产出作者材质definition/instance及独立Unlit预览，固定图像与shader的内容版本。
    class MaterialCooker final {
      public:
        static bool Cook(AssetBuildContext&, const ImportedAsset&, const AssetRecord& shader,
                         const std::map<AssetId, AssetRecord>& ready, std::vector<AssetBuildProduct>&,
                         std::string& error);
    };
}
