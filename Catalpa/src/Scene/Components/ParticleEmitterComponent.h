#pragma once
#include "Particles/ParticleEffect.h"
#include "ECS/World.h"
namespace Catalpa {
    struct ParticleEmitterComponent {
        AssetId effect;
        bool enabled = true, emitting = true;
        uint32_t seedOverride = 0; // 0 表示复用资产 seed；重启键只清空本实例，不改资产。
        uint64_t restartRevision = 1;
        AssetVersionRef<ParticleEffect> effectVersion;
    };
    template <> struct ComponentTraits<ParticleEmitterComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
