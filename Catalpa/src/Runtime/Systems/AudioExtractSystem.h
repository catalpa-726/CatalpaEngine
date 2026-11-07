#pragma once
#include "Audio/AudioService.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa {
    class Scene;
    class CATALPA_API AudioExtractSystem final {
      public:
        bool Update(SceneHandle, const Scene*, AssetManager&, AudioService&, std::string& error);
        void Stop(AudioService&) noexcept;
        // 只退役本场景拥有的 voice；流送 worker/回调脱离前返回 Busy，调用者保留包挂载后重试。
        bool StopAndDrain(AudioService&, std::string& error);

      private:
        bool PumpRetiring(AudioService&, std::string& error);
        struct Retiring {
            AudioVoiceHandle voice;
            bool queued = false;
        };
        struct Source {
            AudioVoiceHandle voice;
            AssetId clip;
            uint64_t version = 0;
            AudioVoiceSettings settings;
        };
        SceneHandle m_Scene;
        uint64_t m_World = 0;
        std::map<PersistentEntityId, Source> m_Sources;
        std::array<Retiring, 256> m_Retiring{};
        size_t m_RetiringCount = 0;
        uint64_t m_StoppingWorld = 0;
    };
} // namespace Catalpa
