#pragma once
#include "Audio/AudioTypes.h"
namespace Catalpa {
    class AudioStreamBuffer;
    class IAudioBackend {
      public:
        virtual ~IAudioBackend() = default;
        virtual bool Start(AudioVoiceHandle, const AssetVersionRef<AudioClipAsset>&,
                           const AudioVoiceSettings&, std::string&) = 0;
        virtual bool StartStream(AudioVoiceHandle, const AssetVersionRef<AudioClipAsset>&,
                                 std::shared_ptr<AudioStreamBuffer>, const AudioVoiceSettings&, bool paused,
                                 std::string&) = 0;
        virtual bool Stop(AudioVoiceHandle, std::string&) = 0; // 成功后 callback 已不能读取此 voice。
        virtual bool Seek(AudioVoiceHandle, uint64_t frame, std::string&) = 0;
        virtual bool Pause(AudioVoiceHandle, bool paused, std::string&) = 0;
        virtual bool Position(AudioVoiceHandle, const Math::Vec3&, std::string&) = 0;
        virtual void Listener(const AudioListenerState&) = 0;
        virtual void MasterGain(float) = 0;
        virtual bool Healthy(std::string& error) const = 0;
        virtual void Advance(double seconds) = 0;
        virtual std::vector<AudioVoiceSnapshot> Snapshot() const = 0;
        virtual void Shutdown() noexcept = 0;
    };
} // namespace Catalpa
