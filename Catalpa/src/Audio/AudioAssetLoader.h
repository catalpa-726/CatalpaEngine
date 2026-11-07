#pragma once
#include "Audio/AudioClipAsset.h"
namespace Catalpa {
    class AssetManager;
    class CATALPA_API AudioAssetLoader final {
      public:
        static bool Encode(const AudioClipAsset&, std::vector<std::byte>&, std::string& error);
        static bool EncodeChunk(std::span<const float>, std::vector<std::byte>&, std::string& error);
        static bool DecodeChunk(const AssetContainer&, AudioChunkAsset&, std::string& error);
        static bool Register(AssetManager&, std::string& error);
    };
} // namespace Catalpa
