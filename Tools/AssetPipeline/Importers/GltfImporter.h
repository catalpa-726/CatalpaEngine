#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    // 将 glTF/GLB 静态场景导入为模型、网格和材质。外部缓冲和贴图由只读源根解析。
    class GltfImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "fastgltf/core-v4-cani1-cskn2-lod-mips-bc1";
        }

        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
    };
}
