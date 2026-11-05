#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
    // 独立于具体宿主的UTF-8参数适配；所有路径显式提供，不从Box/cwd推导。
    class AssetCompilerOptions final {
      public:
        static bool Parse(std::span<const std::string>, AssetBuildRequest&, std::string& error);
    };
}
