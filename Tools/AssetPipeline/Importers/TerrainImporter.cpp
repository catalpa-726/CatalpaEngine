#include "AssetPipeline/Importers/TerrainImporter.h"
#include "AssetPipeline/Navigation/RecastNavMeshBaker.h"
#include "AssetPipeline/Navigation/NavigationBuildProfiles.h"
#include "Terrain/TerrainAsset.h"
#include <bit>

namespace Catalpa::Tools {
    bool TerrainImporter::Import(AssetBuildContext& context, AssetImportResult& output,
                                 std::string& error) const try {
        std::vector<std::byte> author, source;
        if (!context.Read(context.request.source, author, error))
            return false;
        TerrainSource settings;
        if (!ReadTerrainSource({reinterpret_cast<const char*>(author.data()), author.size()}, settings,
                               error))
            return false;
        std::filesystem::path heightPath;
        if (!context.Resolve(context.request.source, settings.heightSource, heightPath, error,
                             SourceReferenceSyntax::FilePath) ||
            !context.Read(heightPath, source, error))
            return false;
        const auto count = size_t(settings.settings.width) * settings.settings.height;
        const uint32_t bytesPerSample = settings.encoding == "f32_le" ? 4u : 2u;
        if (source.size() != count * bytesPerSample) {
            error = "Terrain height source byte count differs from width * height * encoding size";
            return false;
        }
        if (!context.ReserveWorking(source.size() + count * sizeof(float), error))
            return false;
        TerrainHeightData heights;
        heights.settings = settings.settings;
        heights.heights.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            uint32_t bits = 0;
            for (uint32_t b = 0; b < bytesPerSample; ++b)
                bits |= uint32_t(std::to_integer<uint8_t>(source[i * bytesPerSample + b])) << (b * 8);
            const double raw =
                bytesPerSample == 4 ? double(std::bit_cast<float>(bits)) : double(bits) / 65535.0;
            const double h = raw * settings.settings.heightScale + settings.settings.heightOffset;
            if (!std::isfinite(h) || std::abs(h) > 100000) {
                error = "Terrain height scale/offset produces invalid metre-space sample";
                return false;
            }
            heights.heights.push_back(float(h));
        }
        if (!ValidateTerrainHeights(heights, error))
            return false;
        TerrainAsset metadata;
        metadata.settings = settings.settings;
        metadata.heightDigest = AssetBuildDigest(source);
        AssetImportResult result;
        std::vector<AssetId> dependencies;
        const auto append = [&](std::string key, std::string type, const std::string& text) {
            const auto id = context.Identity(key);
            auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            result.assets.push_back(
                {id, key, key, ImportedAuthorProduct{type, {bytes.begin(), bytes.end()}, {}}});
            result.mappings.push_back({id, key, key, type});
            dependencies.push_back(id);
            return id;
        };
        if (settings.material.HasValue()) {
            metadata.material.id = settings.material;
            dependencies.push_back(settings.material);
        } else {
            metadata.material.id = context.Identity("terrain/material");
            ImportedMaterial material;
            material.baseColor = {0.25f, 0.4f, 0.18f, 1};
            material.roughness = 0.9f;
            result.assets.push_back({metadata.material.id, "terrain/material", "Terrain surface", material});
            result.mappings.push_back(
                {metadata.material.id, "terrain/material", "Terrain surface", "material_instance"});
            dependencies.push_back(metadata.material.id);
        }
        const uint32_t width = (metadata.settings.width - 1) / metadata.settings.tileQuads,
                       height = (metadata.settings.height - 1) / metadata.settings.tileQuads;
        for (uint32_t z = 0; z < height; ++z)
            for (uint32_t x = 0; x < width; ++x) {
                if (!context.CheckCancelled(error))
                    return false;
                const auto key = "terrain/" + std::to_string(x) + "/" + std::to_string(z);
                AssetId collision;
                if (metadata.settings.buildCollision) {
                    CollisionShapeAsset shape;
                    std::string text;
                    if (!BuildTerrainCollision(heights, x, z, shape, error) ||
                        !WriteCollisionShape(shape, text, error) ||
                        !context.ReserveWorking(text.size() * 2, error))
                        return false;
                    collision = append(key + "/collision", "collision_shape", text);
                }
                for (uint32_t lod = 0; lod < metadata.settings.lodCount; ++lod) {
                    MeshAsset mesh;
                    if (!BuildTerrainTile(heights, x, z, lod, mesh, error))
                        return false;
                    const auto& data = mesh.mesh->GetData();
                    if (!context.ReserveWorking(data.vertexData.size() + data.indexData.size(), error))
                        return false;
                    const auto name = key + "/lod/" + std::to_string(lod);
                    const auto id = context.Identity(name);
                    metadata.tiles.push_back({x, z, lod, {id, 0, {}}, {collision, 0, {}}, data.localBounds});
                    result.assets.push_back({id, name, name, std::move(mesh)});
                    result.mappings.push_back({id, name, name, "mesh"});
                    dependencies.push_back(id);
                }
            }
        if (metadata.settings.buildNavigation) {
            std::vector<std::byte> profiles;
            if (!context.ReadConfiguration("NavigationBuildProfiles.json", profiles, error))
                return false;
            NavigationBuildInput defaults;
            // 最小三角形仅复用profile选择/验证，不参与地形烘焙；实际几何始终来自原始高度采样。
            constexpr std::string_view profileProbe =
                R"({"schemaVersion":1,"vertices":[[0,0,0],[1,0,0],[0,0,1]],"indices":[0,2,1]})";
            if (!ResolveNavigationBuildSource(
                    profileProbe, {reinterpret_cast<const char*>(profiles.data()), profiles.size()}, defaults,
                    error))
                return false;
            NavigationBuildInput geometry;
            geometry.settings = defaults.settings;
            geometry.settings.cellSize = std::max(0.1f, metadata.settings.spacing * 0.5f);
            for (uint32_t z = 0; z < heights.settings.height; ++z)
                for (uint32_t x = 0; x < heights.settings.width; ++x)
                    geometry.vertices.push_back({x * heights.settings.spacing,
                                                 heights.heights[size_t(z) * heights.settings.width + x],
                                                 z * heights.settings.spacing});
            for (uint32_t z = 0; z + 1 < heights.settings.height; ++z)
                for (uint32_t x = 0; x + 1 < heights.settings.width; ++x) {
                    const auto a = z * heights.settings.width + x, b = a + 1, c = a + heights.settings.width,
                               d = c + 1;
                    geometry.indices.insert(geometry.indices.end(), {a, c, b, b, c, d});
                }
            NavMeshAsset navigation;
            std::string text;
            if (!context.WorkspaceFits(256 * 1024 * 1024, error) ||
                !RecastNavMeshBaker::Bake(geometry, context.request.cancellation, navigation, error) ||
                !WriteNavMeshAsset(navigation, text, error))
                return false;
            metadata.navigation.id = append("terrain/navigation", "navmesh", text);
        }
        std::string text;
        if (!WriteTerrainAsset(metadata, text, error) || !context.CheckCancelled(error))
            return false;
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        result.primary = context.Identity("terrain");
        auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        result.assets.push_back(
            {result.primary, "terrain", "Terrain",
             ImportedAuthorProduct{"terrain", {bytes.begin(), bytes.end()}, std::move(dependencies)}});
        result.mappings.push_back({result.primary, "terrain", "Terrain", "terrain"});
        context.Diagnose(
            AssetDiagnosticLevel::Info, "terrain",
            "Global shared edge samples and global-difference normals; downward skirts mask cross-LOD "
            "cracks; finest collision excludes skirts; source height bytes unchanged");
        output = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa::Tools
