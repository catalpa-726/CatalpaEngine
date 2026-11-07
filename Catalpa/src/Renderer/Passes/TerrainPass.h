#pragma once
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {
    struct RenderTerrainTile {
        RenderInstanceId owner;
        uint64_t tileKey = 0;
        AssetVersionRef<MeshAsset> mesh;
        AssetVersionRef<MaterialInstance> material;
        Math::Mat4 world{1};
    };
    class TerrainPass {
      public:
        static bool Append(RenderFramePacket&, std::span<const RenderTerrainTile>, std::string&);
    };
} // namespace Catalpa
