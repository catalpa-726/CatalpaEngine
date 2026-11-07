#include "catpch.h"
#include "ParticlePass.h"
#include "Renderer/RenderScene.h"
#include "Geometry/MeshValidation.h"
namespace Catalpa {
    bool ParticlePass::CreateQuad(AssetVersionRef<MeshAsset>& output, std::string& error) {
        MeshData data;
        data.vertexCount = 4;
        data.vertexStride = 48;
        data.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position},
                           {1, VertexFormat::Float3, 12, VertexSemantic::Normal},
                           {2, VertexFormat::Float4, 24, VertexSemantic::Tangent},
                           {3, VertexFormat::Float2, 40, VertexSemantic::TexCoord0}};
        const std::array<std::array<float, 12>, 4> vertices{{{-0.5f, -0.5f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                                                             {0.5f, -0.5f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                                                             {0.5f, 0.5f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                                                             {-0.5f, 0.5f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}}};
        const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        const auto vertexBytes = std::as_bytes(std::span{vertices});
        data.vertexData.assign(vertexBytes.begin(), vertexBytes.end());
        const auto indexBytes = std::as_bytes(std::span{indices});
        data.indexData.assign(indexBytes.begin(), indexBytes.end());
        data.localBounds = {{-.5f, -.5f, 0}, {.5f, .5f, 0}, false};
        data.submeshes.push_back({0, 6, 0, 0, 4, 0, MeshTopology::TriangleList});
        MeshAsset asset;
        asset.materialSlots = {"particle"};
        const auto status = CreateMeshResource(data, asset.mesh);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        return AssetVersionRef<MeshAsset>::TryCreate(AssetId::FromWords(0x4341545041525449ULL, 1), 1,
                                                     std::make_shared<const MeshAsset>(std::move(asset)),
                                                     output, error);
    }
    bool ParticlePass::Append(RenderFramePacket& packet, std::span<const RenderParticleEmitter> emitters,
                              const AssetVersionRef<MeshAsset>& quad, std::string& error) {
        if (packet.views.empty())
            return true;
        const auto& camera = packet.views.front().matrices;
        size_t total = 0;
        for (const auto& emitter : emitters)
            total += emitter.particles.size();
        if (total > 16384) {
            error = "Visible particle draw budget exceeds 16384";
            return false;
        }
        for (const auto& emitter : emitters)
            for (const auto& particle : emitter.particles) {
                if (!(particle.size > 0) || particle.color.w <= 0)
                    continue;
                const auto entity = emitter.owner;
                RenderInstanceId id{entity.scene, entity.index, entity.generation,
                                    (uint64_t(1) << 63) | particle.serial};
                const float c = std::cos(particle.rotation), s = std::sin(particle.rotation);
                Math::Mat4 world{1};
                world[0] = Math::Vec4{(camera.right * c + camera.up * s) * particle.size, 0};
                world[1] = Math::Vec4{(-camera.right * s + camera.up * c) * particle.size, 0};
                world[2] = Math::Vec4{-camera.forward, 0};
                world[3] = Math::Vec4{particle.position, 1};
                RenderMeshInstance instance;
                const auto status =
                    BuildRenderMeshInstance(id, quad.Get()->mesh, world, particle.color, instance);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                instance.meshVersion = quad;
                instance.materials = {emitter.material};
                packet.transforms.push_back({id, world});
                packet.instances.push_back(std::move(instance));
            }
        error.clear();
        return true;
    }
} // namespace Catalpa
