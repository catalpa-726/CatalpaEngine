#pragma once
#include "AssetPipeline/IAssetImporter.h"
namespace Catalpa::Tools {
    class FontImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "FreeType-font-v1";
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;
    };
} // namespace Catalpa::Tools
