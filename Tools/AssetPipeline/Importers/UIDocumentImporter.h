#pragma once
#include "AssetPipeline/IAssetImporter.h"
namespace Catalpa::Tools {
    class UIDocumentImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "RmlUi-document-bundle-v1";
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;
    };
} // namespace Catalpa::Tools
