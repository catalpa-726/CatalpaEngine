#pragma once
#include "Images/ImageAssetLoader.h"

namespace Catalpa::Tools {
    // 当前产物为受检RGBA8单层图像，不隐式烘焙mip/压缩或改写原图。
    class TextureCooker final {
      public:
        static bool Encode(const ImageAsset&, std::vector<std::byte>&, std::string&);
    };
}
