#include "catpch.h"
#include "Audio/AudioAssetLoader.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
    bool ValidateAudioClip(const AudioClipAsset& value, std::string& error) {
        if (value.sampleRate != 48000 || value.channels != 2 || value.chunkFrames != 65536 ||
            !value.Frames() || value.Frames() > uint64_t(65536) * 1024 || value.sourceUri.size() > 32760) {
            error = "Invalid canonical audio metadata";
            return false;
        }
        if (value.Streaming()) {
            if (!value.samples.empty() || value.chunks.size() != (value.Frames() + 65535) / 65536) {
                error = "Audio chunk seek table/count mismatch";
                return false;
            }
            std::set<AssetId> ids;
            for (const auto& chunk : value.chunks)
                if (!chunk.id.HasValue() || !ids.insert(chunk.id).second) {
                    error = "Invalid audio chunk identity";
                    return false;
                }
        } else {
            if (value.samples.empty() || value.samples.size() != value.Frames() * 2 ||
                value.samples.size() > 16 * 1024 * 1024) {
                error = "Resident PCM byte budget exceeded";
                return false;
            }
            for (float sample : value.samples)
                if (!std::isfinite(sample) || std::abs(sample) > 16) {
                    error = "PCM is outside finite [-16,16] headroom";
                    return false;
                }
        }
        error.clear();
        return true;
    }
    bool AudioAssetLoader::EncodeChunk(std::span<const float> samples, std::vector<std::byte>& out,
                                       std::string& error) {
        if (samples.empty() || samples.size() % 2 || samples.size() > 65536 * 2) {
            error = "Invalid audio chunk frame count";
            return false;
        }
        BinaryWriter w(1024 * 1024);
        w.U32(uint32_t(samples.size() / 2));
        for (float v : samples)
            if (!std::isfinite(v) || std::abs(v) > 16 || !w.F32(v)) {
                error = "Invalid audio chunk sample";
                return false;
            }
        out = w.Data();
        error.clear();
        return true;
    }
    bool AudioAssetLoader::DecodeChunk(const AssetContainer& c, AudioChunkAsset& out, std::string& error) {
        BinaryReader r(c.payload);
        uint32_t frames = 0;
        if (c.payloadVersion != 1 || c.type.ToString() != "audio_chunk" || !c.dependencies.empty() ||
            !r.U32(frames) || !frames || frames > 65536 || r.Remaining() != size_t(frames) * 8) {
            error = "Invalid audio chunk container";
            return false;
        }
        AudioChunkAsset candidate;
        candidate.samples.resize(size_t(frames) * 2);
        for (auto& v : candidate.samples)
            if (!r.F32(v) || !std::isfinite(v) || std::abs(v) > 16) {
                error = "Invalid audio chunk PCM";
                return false;
            }
        out = std::move(candidate);
        error.clear();
        return true;
    }
    bool AudioAssetLoader::Encode(const AudioClipAsset& value, std::vector<std::byte>& out,
                                  std::string& error) {
        if (!ValidateAudioClip(value, error))
            return false;
        BinaryWriter w(65 * 1024 * 1024);
        w.String(value.sourceUri);
        w.U32(value.sampleRate);
        w.U32(value.channels);
        w.U32(value.chunkFrames);
        w.U64(value.Frames());
        w.U8(value.Streaming() ? 1 : 0);
        if (value.Streaming()) {
            w.U32(uint32_t(value.chunks.size()));
            for (const auto& d : value.chunks)
                w.String(d.id.ToString());
        } else
            for (float sample : value.samples)
                w.F32(sample);
        if (!w.Error().empty()) {
            error = w.Error();
            return false;
        }
        out = w.Data();
        error.clear();
        return true;
    }
    bool AudioAssetLoader::Register(AssetManager& manager, std::string& error) {
        AssetLoader chunk;
        AssetType::TryParse("audio_chunk", chunk.type);
        chunk.cppType = typeid(AudioChunkAsset);
        chunk.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& out,
                          std::string& e) {
            if (c.payload.size() > context.maxResidentBytes / 2 ||
                context.cancellation.IsCancellationRequested()) {
                e = "Audio chunk budget/cancellation";
                return false;
            }
            AudioChunkAsset value;
            if (!DecodeChunk(c, value, e))
                return false;
            auto size = sizeof(AudioChunkAsset) + value.samples.capacity() * sizeof(float);
            out = MakeAssetLoadResult<AudioChunkAsset>(
                std::make_shared<const AudioChunkAsset>(std::move(value)), size);
            return true;
        };
        if (!manager.RegisterLoader(std::move(chunk), error))
            return false;
        AssetLoader loader;
        AssetType::TryParse("audio_clip", loader.type);
        loader.cppType = typeid(AudioClipAsset);
        loader.deferredDependencies = true;
        loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& out,
                           std::string& e) {
            if (c.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
                c.payload.size() > context.maxResidentBytes / 2) {
                e = "Audio version/budget/cancellation rejected";
                return false;
            }
            BinaryReader r(c.payload);
            AudioClipAsset clip;
            uint8_t stream = 0;
            if (!r.String(clip.sourceUri, 32760) || !r.U32(clip.sampleRate) || !r.U32(clip.channels) ||
                !r.U32(clip.chunkFrames) || !r.U64(clip.frameCount) || !r.U8(stream) || stream > 1) {
                e = "Truncated audio metadata";
                return false;
            }
            if (stream) {
                uint32_t count = 0;
                if (!r.U32(count) || !count || count > 1024 || count != context.declaredDependencies.size()) {
                    e = "Invalid audio seek table";
                    return false;
                }
                for (uint32_t i = 0; i < count; ++i) {
                    std::string text;
                    AssetId id;
                    if (!r.String(text, 36) || !AssetId::TryParse(text, id)) {
                        e = "Invalid audio chunk ID";
                        return false;
                    }
                    auto found =
                        std::find_if(context.declaredDependencies.begin(), context.declaredDependencies.end(),
                                     [&](const auto& d) { return d.id == id; });
                    if (found == context.declaredDependencies.end()) {
                        e = "Audio chunk not declared";
                        return false;
                    }
                    clip.chunks.push_back(*found);
                }
                clip.readDeferred = context.readDeferred;
                if (!clip.readDeferred) {
                    e = "Streaming audio requires pinned deferred IO";
                    return false;
                }
            } else {
                if (!c.dependencies.empty() || clip.frameCount > 8 * 1024 * 1024 ||
                    clip.frameCount > context.maxResidentBytes / 8 || r.Remaining() != clip.frameCount * 8) {
                    e = "Audio PCM range rejected";
                    return false;
                }
                clip.samples.resize(size_t(clip.frameCount) * 2);
                for (auto& v : clip.samples)
                    if (!r.F32(v)) {
                        e = r.Error();
                        return false;
                    }
            }
            if (!r.AtEnd() || !ValidateAudioClip(clip, e))
                return false;
            auto used = sizeof(AudioClipAsset) + clip.sourceUri.capacity() + clip.samples.capacity() * 4 +
                        clip.chunks.capacity() * sizeof(AssetDependency);
            out = MakeAssetLoadResult<AudioClipAsset>(std::make_shared<const AudioClipAsset>(std::move(clip)),
                                                      used);
            e.clear();
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
} // namespace Catalpa
