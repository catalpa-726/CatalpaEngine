#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
    // 命令行与编辑器共用的同步构建入口。主清单发布前响应取消。
    class AssetCooker final {
      public:
        static AssetBuildResult Build(AssetBuildRequest);
    };
}
