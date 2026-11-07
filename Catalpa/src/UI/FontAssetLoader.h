#pragma once
#include "UI/FontAsset.h"
namespace Catalpa {
    class AssetManager;
    class CATALPA_API FontAssetLoader final {
      public:
        static bool Encode(const FontAsset&, std::vector<std::byte>&, std::string& error);
        static bool Register(AssetManager&, std::string& error);
    };
} // namespace Catalpa
