#pragma once
#include "Audio/Backends/IAudioBackend.h"
#include "Scene/SceneHandle.h"
namespace Catalpa {
    class AssetManager;
    class JobSystem;
    class Scene;
    class CATALPA_API AudioService final {
      public:
        AudioService();
        ~AudioService();
        void BindJobs(JobSystem&);
        bool Initialize(AudioServiceConfig, std::string& error);
        bool Play(const AssetVersionRef<AudioClipAsset>&, AudioVoiceSettings, AudioVoiceHandle&,
                  std::string& error);
        bool Stop(AudioVoiceHandle, std::string& error);
        // 预览宿主关闭时移交退役责任；有界地等待命令额度，不要求已销毁的宿主继续重试。
        bool Retire(AudioVoiceHandle, std::string& error);
        bool Seek(AudioVoiceHandle, uint64_t frame, std::string& error);
        bool Pause(AudioVoiceHandle, bool paused, std::string& error);
        bool SetPosition(AudioVoiceHandle, Math::Vec3, std::string& error);
        bool SetListener(AudioListenerState, std::string& error);
        bool SetMasterGain(float, std::string& error);
        void Update(double seconds);
        std::vector<AudioAcknowledgement> TakeAcknowledgements();
        std::vector<AudioVoiceSnapshot> Snapshot() const;
        const std::string& Diagnostic() const noexcept;
        bool IsNull() const noexcept;
        bool HasVoice(AudioVoiceHandle) const noexcept;
        void Shutdown() noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
