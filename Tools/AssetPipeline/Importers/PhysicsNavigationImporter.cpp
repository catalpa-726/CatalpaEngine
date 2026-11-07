#include "AssetPipeline/Importers/PhysicsNavigationImporter.h"
#include "AssetPipeline/Navigation/RecastNavMeshBaker.h"
#include "Physics/PhysicsWorld.h"
#include "AssetPipeline/Navigation/NavigationBuildProfiles.h"

namespace Catalpa::Tools {
    bool PhysicsNavigationImporter::Import(AssetBuildContext& context, AssetImportResult& output,
                                           std::string& error) const try {
        std::vector<std::byte> source;
        if (!context.Read(context.request.source, source, error) || source.size() > 32 * 1024 * 1024 ||
            !context.ReserveWorking(source.size() * 8, error))
            return false;
        const std::string_view input(reinterpret_cast<const char*>(source.data()), source.size());
        std::string canonical;
        if (m_Type == "collision_shape") {
            CollisionShapeAsset shape;
            if (!ReadCollisionShape(input, shape, error))
                return false;
            // 生产阶段也验证真实SDK形状。Cook存可移植领域值，运行时才生成独占SDK对象。
            PhysicsWorld validation;
            PhysicsBodyHandle body;
            if (!validation.Initialize({}, error) || !validation.CreateBody({}, shape, {}, body, error) ||
                !WriteCollisionShape(shape, canonical, error))
                return false;
        } else if (m_Type == "physics_material") {
            PhysicsMaterialAsset material;
            if (!ReadPhysicsMaterial(input, material, error) ||
                !WritePhysicsMaterial(material, canonical, error))
                return false;
        } else if (m_Type == "navmesh") {
            NavigationBuildInput sourceMesh;
            NavMeshAsset navigation;
            std::vector<std::byte> profiles;
            if (!context.ReadConfiguration("NavigationBuildProfiles.json", profiles, error))
                return false;
            const std::string_view configuration(reinterpret_cast<const char*>(profiles.data()),
                                                 profiles.size());
            if (!ResolveNavigationBuildSource(input, configuration, sourceMesh, error) ||
                !context.WorkspaceFits(256 * 1024 * 1024, error) ||
                !RecastNavMeshBaker::Bake(sourceMesh, context.request.cancellation, navigation, error) ||
                !WriteNavMeshAsset(navigation, canonical, error))
                return false;
            context.Diagnose(
                AssetDiagnosticLevel::Info, "navigation",
                "Recast built an owned single Detour tile; source triangles remain read-only; area0 is "
                "default walkable");
        } else {
            error = "Unknown physics/navigation author type";
            return false;
        }
        if (!context.CheckCancelled(error))
            return false;
        const auto id = context.Identity(m_Type);
        auto bytes = std::as_bytes(std::span(canonical.data(), canonical.size()));
        AssetImportResult result;
        result.primary = id;
        result.assets.push_back(
            {id, m_Type, m_Type, ImportedAuthorProduct{m_Type, {bytes.begin(), bytes.end()}, {}}});
        result.mappings.push_back({id, m_Type, m_Type, m_Type});
        output = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa::Tools
