#pragma once
#include "Assets/PackageFormat.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
    class PackageBuilder final {
      public:
        // 挂载表由项目配置显式构造；闭包中的每个精确版本必须可读取。
        static bool Build(const AssetRegistry&, const VirtualFileSystem&, AssetId entryScene,
                          const std::filesystem::path& absoluteOutput, CancellationToken, std::string& error,
                          PackageLimits limits = {});
    };
} // namespace Catalpa::Tools
