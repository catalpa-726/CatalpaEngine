#pragma once
#include "Renderer/RenderFramePacket.h"
#include "Particles/ParticleEffect.h"
namespace Catalpa {
    struct RenderParticleEmitter {
        RenderInstanceId owner;
        AssetVersionRef<MaterialInstance> material;
        std::vector<ParticleBillboard> particles;
    };
    // 生成camera-facing实例，由已有透明排序和材质Pass消费；不另建渲染器。
    class ParticlePass {
      public:
        static bool CreateQuad(AssetVersionRef<MeshAsset>&, std::string&);
        static bool Append(RenderFramePacket&, std::span<const RenderParticleEmitter>,
                           const AssetVersionRef<MeshAsset>&, std::string&);
    };
} // namespace Catalpa
