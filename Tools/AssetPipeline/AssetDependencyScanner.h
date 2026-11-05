#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 按规范路径排序已打开的输入，用内容摘要和构建配置生成缓存键。
    class AssetDependencyScanner final {
      public:
        static std::vector<AssetSourceDependency> Snapshot(const AssetBuildContext&);
        static std::string Key(const AssetBuildContext&, std::string_view signature);
    };
}
