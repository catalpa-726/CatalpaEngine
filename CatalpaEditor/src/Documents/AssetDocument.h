#pragma once
#include "Documents/Document.h"
#include "Materials/MaterialInstance.h"
#include "Images/ImageAsset.h"
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Editor {
    // 材质草稿独立于已发布资产；纹理文档只持有确切版本，无写原图入口。
    class AssetDocument final : public Document {
      public:
        AssetDocument(StableId id, AssetVersionRef<MaterialInstance> source);
        AssetDocument(StableId id, AssetVersionRef<ImageAsset> source);
        AssetDocument(StableId id, AssetId asset, std::string type, std::string source);
        bool Serialize(std::string&, std::string& error) const override;
        const std::shared_ptr<const std::string>& Source() const noexcept { return m_Source; }
        bool ReplaceSource(const std::shared_ptr<const std::string>& expected,
                           std::shared_ptr<const std::string> next, std::string& error);

        AssetId Asset() const noexcept {
            return m_Asset;
        }
        void SetSourceIdentity(AssetId id) { if (m_Source && id.HasValue()) m_Asset = id; }

        // 与 Cook 记录共用稳定类型键；文档固定版本与显示用编辑器分别管理。
        std::string_view TypeKey() const noexcept override {
            return m_Material ? "material_instance" : (m_Image.HasValue() ? "image" : std::string_view(m_Type));
        }

        uint64_t SourceVersion() const noexcept {
            return m_Version;
        }

        const auto& Material() const noexcept {
            return m_Material;
        }

        const ImageAsset* Image() const noexcept {
            return m_Image.Get();
        }

        const AssetVersionRef<ImageAsset>& ImageVersion() const noexcept {
            return m_Image;
        }

        const auto& Recipe() const noexcept {
            return m_Recipe;
        }

        void AttachRecipe(const std::shared_ptr<const Tools::AssetBuildRequest>& recipe) {
            if (!m_Recipe)
                m_Recipe = recipe;
        }

        void SetCookReport(const Tools::AssetBuildResult& result) {
            m_Report = result.report;
            m_Diagnostics = result.diagnostics;
        }

        const auto& Report() const noexcept {
            return m_Report;
        }

        const auto& Diagnostics() const noexcept {
            return m_Diagnostics;
        }

        bool ReplaceRecipe(const std::shared_ptr<const Tools::AssetBuildRequest>& expected,
                           std::shared_ptr<const Tools::AssetBuildRequest> next, std::string& error);
        bool ReplaceMaterial(const std::shared_ptr<const MaterialInstance>& expected,
                             std::shared_ptr<const MaterialInstance> next, std::string& error);

      private:
        AssetId m_Asset;
        uint64_t m_Version;
        std::shared_ptr<const MaterialInstance> m_Material;
        AssetVersionRef<ImageAsset> m_Image;
        std::shared_ptr<const Tools::AssetBuildRequest> m_Recipe;
        std::filesystem::path m_Report;
        std::vector<Tools::AssetBuildDiagnostic> m_Diagnostics;
        std::string m_Type;
        std::shared_ptr<const std::string> m_Source;
    };
}
