#pragma once
#include "Geometry/GeometryAssetLoader.h"

namespace Catalpa::Tools {
    // 复用Runtime模型协议编码层级及作者mesh/material身份，不烘焙世界变换或预览覆盖。
    class ModelCooker final {
      public:
        static bool Encode(const ModelAsset&, std::vector<std::byte>&, std::string&);
    };
}
