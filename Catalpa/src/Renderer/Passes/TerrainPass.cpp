#include "catpch.h"
#include "TerrainPass.h"
#include "Renderer/RenderScene.h"
namespace Catalpa {
    bool TerrainPass::Append(RenderFramePacket& packet, std::span<const RenderTerrainTile> tiles,
                             std::string& error) {
        for (const auto& tile : tiles) {
            if (!tile.mesh.HasValue() || !tile.material.HasValue() ||
                tile.tileKey >= (uint64_t(1) << 63) - 1) {
                error = "Invalid terrain tile snapshot";
                return false;
            }
            const auto entity = tile.owner;
            RenderInstanceId id{entity.scene, entity.index, entity.generation, tile.tileKey + 1};
            RenderMeshInstance instance;
            const auto status =
                BuildRenderMeshInstance(id, tile.mesh.Get()->mesh, tile.world, Math::Vec4{1}, instance);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            instance.meshVersion = tile.mesh;
            instance.materials = {tile.material};
            packet.transforms.push_back({id, tile.world});
            packet.instances.push_back(std::move(instance));
        }
        error.clear();
        return true;
    }
} // namespace Catalpa
