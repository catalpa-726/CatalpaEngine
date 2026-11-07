#pragma once
#include "Assets/AssetHandle.h"
#include "Materials/MaterialInstance.h"
#include "Core/Math/MathTypes.h"

namespace Catalpa {
    inline constexpr uint32_t MaximumParticleCapacity = 16384;
    struct ParticleEffect {
        AssetId material;
        AssetVersionRef<MaterialInstance> materialVersion;
        uint32_t seed = 1, maxParticles = 1024;
        double emissionRate = 30, duration = 5;
        bool loop = true, localSpace = false;
        float minLifetime = 1, maxLifetime = 2, minSize = .05f, maxSize = .15f;
        Math::Vec3 minVelocity{-1, 1, -1}, maxVelocity{1, 3, 1}, gravity{0, -9.81f, 0};
        Math::Vec4 startColor{1}, endColor{1, 1, 1, 0};
        float endSizeScale = 1, minAngularVelocity = 0, maxAngularVelocity = 0;
    };
    CATALPA_API bool ValidateParticleEffect(const ParticleEffect&, std::string& error);
    CATALPA_API bool ReadParticleEffect(std::string_view, ParticleEffect&, std::string& error);
    CATALPA_API bool WriteParticleEffect(const ParticleEffect&, std::string&, std::string& error);
    class AssetManager;
    CATALPA_API bool RegisterParticleLoader(AssetManager&, std::string& error);

    struct ParticleBillboard {
        Math::Vec3 position{0};
        // size 是世界空间完整边长（米），rotation 是面向相机平面内的弧度。
        float size = 0, rotation = 0;
        Math::Vec4 color{1};
        uint64_t serial = 0;
    };
    // 可单独对照的确定性积分器；粒子池不持有 Scene、AssetManager 或 Renderer 指针。
    class CATALPA_API ParticlePool {
      public:
        bool Reset(const ParticleEffect&, uint32_t seed, std::string& error);
        bool Tick(double dt, bool emitting, const Math::Mat4& emitter, std::string& error);
        std::vector<ParticleBillboard> Snapshot(const Math::Mat4& emitter) const;
        size_t AliveCount() const noexcept {
            return m_Live;
        }
        uint64_t DroppedCount() const noexcept {
            return m_Dropped;
        }

      private:
        struct Particle {
            bool alive = false;
            Math::Vec3 position{0}, velocity{0};
            float age = 0, lifetime = 1, size = 1, rotation = 0, angularVelocity = 0;
            uint64_t serial = 0;
        };
        float Random();
        ParticleEffect m_Effect;
        std::vector<Particle> m_Particles;
        uint32_t m_Random = 1;
        size_t m_Live = 0;
        double m_Time = 0, m_Fraction = 0;
        uint64_t m_Serial = 0, m_Dropped = 0;
    };
} // namespace Catalpa
