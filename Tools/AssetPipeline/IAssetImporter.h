#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
    // 导入器读取源文件，生成拥有独立存储的 CPU 领域数据并记录输入依赖。
    class IAssetImporter {
      public:
        virtual ~IAssetImporter() = default;
        virtual std::string_view Name() const noexcept = 0;
        virtual bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const = 0;
    };
}
