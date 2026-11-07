#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    // 领域解析和引用收集在工具层完成；运行时只读取已 Cook 的固定版本容器。
    class AnimationImporter final : public IAssetImporter {
      public:
        explicit AnimationImporter(std::string type)
            : m_Type(std::move(type)), m_Name("author-animation-" + m_Type + "-v3-cani1-cskn2") {}
        std::string_view Name() const noexcept override {
            return m_Name;
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;

      private:
        std::string m_Type, m_Name;
    };
} // namespace Catalpa::Tools
