#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
    class AssetArchiveWriter final {
      public:
        // 容器的内容版本同时取决于载荷和每个直接依赖的固定版本。
        static bool Encode(AssetId, std::string_view type, std::span<const std::byte> payload,
                           std::span<const AssetRecord> dependencies, AssetBuildProduct&, std::string& error);
    };
}
