#pragma once
#include "Images/ImageAssetLoader.h"

namespace Catalpa::Tools {
    struct TextureCookReport {
        bool mipmaps = false, compressed = false;
        double linearRmse = 0, maximumLinearError = 0;
        std::string policy;
    };
    // 只处理派生 Cook 值：原始解码像素及源文件保持不变。
    class TextureCooker final {
      public:
        static constexpr std::string_view Signature = "rgba8-mips-area-linear-premult-normal-v1-bc1-rmse008";
        static bool Prepare(const ImageAsset&, ImageAsset&, TextureCookReport&, std::string&);
        static bool Encode(const ImageAsset&, std::vector<std::byte>&, std::string&,
                           TextureCookReport* = nullptr);
    };
} // namespace Catalpa::Tools
