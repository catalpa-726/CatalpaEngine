#pragma once
#include "AssetPipeline/IAssetImporter.h"
#include <utility>

namespace Catalpa::Tools {
    // 将内存里的八位图像解码为 RGBA8，保留像素行序，并按用途标明颜色空间。
    class TextureImporter final : public IAssetImporter {
      public:
        std::string_view Name() const noexcept override {
            return "stb-image/rgba8-v1";
        }

        bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
        // 同一张原图可按不同材质通道使用。分类和派生 ID 后缀一同返回，保证二者一致。
        static std::pair<ImageSemantic, std::string> MaterialChannel(std::string_view channel);
        static bool Decode(AssetBuildContext&, std::span<const std::byte>, std::string source, ImageSemantic,
                           ImageAsset&, std::string&);
    };
}
