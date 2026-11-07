#include "catpch.h"
#include "Audio/Null/NullAudioBackend.h"
#include "Audio/AudioStreamService.h"
namespace Catalpa {
    namespace {
        class NullAudioBackend final : public IAudioBackend {
            struct Voice {
                AssetVersionRef<AudioClipAsset> clip;
                AudioVoiceSettings settings;
                double cursor = 0;
                bool paused = false;
                std::shared_ptr<AudioStreamBuffer> stream;
                double fraction = 0;
            };
            std::map<AudioVoiceHandle, Voice> voices;

          public:
            bool Start(AudioVoiceHandle id, const AssetVersionRef<AudioClipAsset>& clip,
                       const AudioVoiceSettings& settings, std::string& error) override {
                if (!voices.emplace(id, Voice{clip, settings}).second) {
                    error = "Duplicate null voice";
                    return false;
                }
                error.clear();
                return true;
            }
            bool StartStream(AudioVoiceHandle id, const AssetVersionRef<AudioClipAsset>& clip,
                             std::shared_ptr<AudioStreamBuffer> buffer, const AudioVoiceSettings& settings,
                             bool paused, std::string& error) override {
                if (!Start(id, clip, settings, error))
                    return false;
                voices.at(id).cursor = double(buffer->Cursor());
                voices.at(id).stream = std::move(buffer);
                voices.at(id).paused = paused;
                return true;
            }
            bool Stop(AudioVoiceHandle id, std::string& error) override {
                voices.erase(id);
                error.clear();
                return true;
            }
            bool Seek(AudioVoiceHandle id, uint64_t frame, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end() || frame > it->second.clip.Get()->Frames()) {
                    error = "Invalid null voice seek";
                    return false;
                }
                it->second.cursor = double(frame);
                error.clear();
                return true;
            }
            bool Pause(AudioVoiceHandle id, bool paused, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end()) {
                    error = "Unknown voice";
                    return false;
                }
                it->second.paused = paused;
                error.clear();
                return true;
            }
            bool Position(AudioVoiceHandle id, const Math::Vec3& p, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end()) {
                    error = "Unknown voice";
                    return false;
                }
                it->second.settings.position = p;
                error.clear();
                return true;
            }
            void Listener(const AudioListenerState&) override {}
            void MasterGain(float) override {}
            bool Healthy(std::string& error) const override {
                error.clear();
                return true;
            }
            void Advance(double seconds) override {
                for (auto& [id, v] : voices)
                    if (!v.paused) {
                        if (v.stream) {
                            const double wanted = v.fraction + seconds * 48000 * v.settings.pitch;
                            uint64_t frames = uint64_t(wanted);
                            v.fraction = wanted - double(frames);
                            while (frames) {
                                const auto request = uint32_t(std::min<uint64_t>(frames, 4096));
                                const auto read = v.stream->Read(nullptr, request);
                                if (!read)
                                    break;
                                frames -= read;
                            }
                            v.cursor = double(v.stream->Cursor());
                            continue;
                        }
                        v.cursor += seconds * v.clip.Get()->sampleRate * v.settings.pitch;
                        const auto length = double(v.clip.Get()->Frames());
                        v.cursor = v.settings.loop ? std::fmod(v.cursor, length) : std::min(v.cursor, length);
                    }
            }
            std::vector<AudioVoiceSnapshot> Snapshot() const override {
                std::vector<AudioVoiceSnapshot> out;
                for (const auto& [id, v] : voices)
                    out.push_back(
                        {id, uint64_t(v.cursor), v.clip.Get()->Frames(),
                         !v.paused && (v.stream ? !v.stream->Ended() : v.cursor < v.clip.Get()->Frames()),
                         v.settings.gain});
                return out;
            }
            void Shutdown() noexcept override {
                voices.clear();
            }
        };
    } // namespace
    std::unique_ptr<IAudioBackend> CreateNullAudioBackend() {
        return std::make_unique<NullAudioBackend>();
    }
} // namespace Catalpa
