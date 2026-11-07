#pragma once
#include "Particles/ParticleEffect.h"
#include "Scene/EntityReference.h"
namespace Catalpa {
    class Scene;
    class AssetManager;
    struct ParticleEmitterSnapshot {
        EntityReference entity;
        AssetVersionRef<MaterialInstance> material;
        std::vector<ParticleBillboard> particles;
        uint64_t dropped = 0;
    };
    class CATALPA_API ParticleSystem {
      public:
        bool Tick(Scene&, AssetManager&, double dt, std::string& error);
        std::vector<ParticleEmitterSnapshot> Snapshot(uint64_t world) const;
        void ReleaseWorld(uint64_t world) noexcept;
        void Stop() noexcept {
            m_Instances.clear();
        }

      private:
        struct Instance {
            EntityReference entity;
            AssetVersionRef<ParticleEffect> effect;
            ParticlePool pool;
            Math::Mat4 emitter{1};
            uint64_t restart = 0;
            uint32_t seed = 0;
            bool visible = true;
        };
        std::map<std::tuple<uint64_t, uint32_t, uint32_t>, Instance> m_Instances;
    };
} // namespace Catalpa
