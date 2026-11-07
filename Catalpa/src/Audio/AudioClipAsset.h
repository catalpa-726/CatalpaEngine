#pragma once
#include "Assets/AssetHandle.h"
#include "Assets/AssetContainerFormat.h"
#include "Core/Base.h"
namespace Catalpa {
    struct AudioChunkAsset {
        std::vector<float> samples;
    };
    // 小片段驻留；长片段只保留 seek 表与不可变 IO 快照，PCM chunk 在 worker 按需读取。
    struct AudioClipAsset {
        std::string sourceUri;
        uint32_t sampleRate = 48000, channels = 2, chunkFrames = 65536;
        uint64_t frameCount = 0;
        std::vector<float> samples;
        std::vector<AssetDependency> chunks;
        std::function<bool(AssetId, size_t, std::vector<std::byte>&, std::string&)> readDeferred;
        uint64_t Frames() const noexcept {
            return frameCount ? frameCount : samples.size() / 2;
        }
        bool Streaming() const noexcept {
            return !chunks.empty();
        }
    };
    CATALPA_API bool ValidateAudioClip(const AudioClipAsset&, std::string& error);
} // namespace Catalpa
