#pragma once
#include "AssetPipeline/IAssetImporter.h"
namespace Catalpa::Tools {
    class AudioImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "miniaudio-pcm48k-stereo-v1";
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
    };
} // namespace Catalpa::Tools
