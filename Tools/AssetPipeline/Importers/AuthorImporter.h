#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    // 作者领域仍分别解析/验证；这里只统一只读输入、身份与容器输出的工具接线。
    class AuthorImporter final : public IAssetImporter {
      public:
        explicit AuthorImporter(std::string type) : m_Type(std::move(type)), m_Name("author-" + m_Type + "-v1") {}
        std::string_view Name() const noexcept override { return m_Name; }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;
      private:
        std::string m_Type, m_Name;
    };
}
