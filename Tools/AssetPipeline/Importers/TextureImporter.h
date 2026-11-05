#pragma once
#include "AssetPipeline/IAssetImporter.h"
#include <utility>

namespace Catalpa::Tools {
    // 八位图像保留RGBA8；Radiance HDR保留线性RGBA32F，不翻转、缩放或归一化原件。
    class TextureImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "stb-image/rgba8-hdr-float-v2";
        }

        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
        // 同一张原图可按不同材质通道使用。分类和派生 ID 后缀一同返回，保证二者一致。
        static std::pair<ImageSemantic, std::string> MaterialChannel(std::string_view channel);
        static bool Decode(AssetBuildContext&, std::span<const std::byte>, std::string source, ImageSemantic,
                           ImageAsset&, std::string&);
    };
}
