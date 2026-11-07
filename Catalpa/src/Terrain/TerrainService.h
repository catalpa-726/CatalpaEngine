#pragma once
#include "Terrain/TerrainAsset.h"
#include "Physics/PhysicsService.h"
#include "Scene/Components/TerrainComponent.h"
#include "Core/Memory/MemoryBudget.h"
namespace Catalpa {
    class JobSystem;
    struct TerrainTileDraw {
        EntityReference owner;
        uint64_t tileKey = 0;
        uint32_t x = 0, z = 0, lod = 0;
        AssetVersionRef<MeshAsset> mesh;
        AssetVersionRef<MaterialInstance> material;
        Math::Mat4 world{1};
    };
    CATALPA_API bool CaptureTerrainComponents(const Scene&, EntityId, DynamicComponentMap&, std::string&);
    CATALPA_API bool RestoreTerrainComponents(Scene&, EntityId, const DynamicComponentMap&, std::string&);
    CATALPA_API bool RestoreTerrainComponents(World&, EntityId, const DynamicComponentMap&, std::string&);
    CATALPA_API bool ValidateTerrainComponents(const DynamicComponentMap&, std::string&);
    CATALPA_API std::vector<AssetId> TerrainComponentDependencies(const DynamicComponentMap&);
    CATALPA_API WorldStatus SetTerrain(Scene&, EntityId, const TerrainComponent&);
    // CPU tile所有权随快照退休；GPU资源仍复用Renderer既有cache与完成集合。
    class CATALPA_API TerrainService final {
      public:
        TerrainService();
        ~TerrainService();
        bool Tick(Scene&, AssetManager&, JobSystem&, Math::Vec3 cameraWorld, PhysicsWorld*, std::string&);
        void Stop() noexcept;
        const std::vector<TerrainTileDraw>& Draws() const noexcept;
        const std::vector<std::string>& Diagnostics() const noexcept;
        MemoryBudgetStats Budget() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
