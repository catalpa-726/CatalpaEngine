#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    // 从 FBX 或 OBJ 读取模型，保留材质和外部文件引用，在模型根节点应用坐标转换。
    class AssimpImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "assimp/fbx-obj-v3";
        }

        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
    };
}
