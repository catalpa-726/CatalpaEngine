#pragma once
#include "AssetPipeline/IAssetImporter.h"
namespace Catalpa::Tools {
    class TerrainImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "heightfield-tiles/2";
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
    };
} // namespace Catalpa::Tools
