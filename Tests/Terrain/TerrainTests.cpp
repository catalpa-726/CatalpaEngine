#include "catpch.h"
#include "TestRunner.h"
#include "Terrain/TerrainService.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Materials/MaterialAssetLoader.h"
#include "Scene/Scene.h"

namespace Catalpa::Tests
{
    void RunTerrainTests(TestContext& test)
    {
        std::string error;
        TerrainHeightData source;
        source.settings.width = source.settings.height = 33;
        source.settings.tileQuads = 16;
        source.settings.lodCount = 3;
        source.settings.buildCollision = false;
        source.heights.resize(33 * 33);
        for (uint32_t z = 0; z < 33; ++z)
            for (uint32_t x = 0; x < 33; ++x)
                source.heights[z * 33 + x] = 0.3f * std::sin(x * 0.1f) * std::cos(z * 0.1f);
        const auto original = source.heights;
        test.Check(ValidateTerrainHeights(source, error),
                   "Terrain validates metre samples and conservative skirt depth");
        MeshAsset left, right, coarse;
        bool generated = BuildTerrainTile(source, 0, 0, 0, left, error) &&
                         BuildTerrainTile(source, 1, 0, 0, right, error) &&
                         BuildTerrainTile(source, 1, 0, 1, coarse, error);
        test.Check(generated, "Generate actual sealed terrain mesh tiles and coarser LOD");
        if (generated) {
            bool seam = true;
            for (size_t z = 0; z < 17; ++z) {
                const auto* a = left.mesh->GetData().vertexData.data() + (z * 17 + 16) * 48;
                const auto* b = right.mesh->GetData().vertexData.data() + (z * 17) * 48;
                seam &= std::memcmp(a, b, 48) == 0;
                if (z % 2 == 0) {
                    const auto* c = coarse.mesh->GetData().vertexData.data() + (z / 2 * 9) * 48;
                    seam &= std::memcmp(a, c, 48) == 0;
                }
            }
            test.Check(seam,
                       "Adjacent tile/LOD shared edge samples, normals, UV and tangents match bit-for-bit");
            test.Check(left.mesh->GetData().localBounds.minimum.y < -31,
                       "Downward skirts extend geometry to mask T-junction cracks");
            CollisionShapeAsset collision;
            test.Check(BuildTerrainCollision(source, 0, 0, collision, error) &&
                           collision.vertices.size() == 17 * 17,
                       "Physics mesh uses finest surface and excludes render-only skirts");
        }
        test.Check(source.heights == original, "Terrain derivation does not modify source height values");
        auto invalid = source;
        invalid.settings.skirtDepth = 0.01f;
        test.Check(!ValidateTerrainHeights(invalid, error),
                   "Insufficient skirt depth is rejected rather than silently normalizing author settings");
        invalid = source;
        invalid.heights[0] = std::numeric_limits<float>::infinity();
        test.Check(!ValidateTerrainHeights(invalid, error), "Terrain rejects nonfinite source sample");
        if (!generated)
            return;

        const auto terrainId = AssetId::FromWords(0x7465727261696e00, 1),
                   materialId = AssetId::FromWords(0x7465727261696e00, 2);
        TerrainAsset metadata;
        metadata.settings = source.settings;
        metadata.heightDigest = std::string(64, 'a');
        metadata.material = {materialId, 1, {}};
        auto files = std::make_shared<std::map<AssetId, std::vector<std::byte>>>();
        auto reads = std::make_shared<std::atomic_size_t>(0);
        for (uint32_t z = 0; z < 2; ++z)
            for (uint32_t x = 0; x < 2; ++x)
                for (uint32_t lod = 0; lod < 3; ++lod) {
                    MeshAsset mesh;
                    std::vector<std::byte> payload, container;
                    const auto id = AssetId::FromWords(0x7465727261696e10, 1 + (z * 2 + x) * 3 + lod);
                    if (!BuildTerrainTile(source, x, z, lod, mesh, error) ||
                        !GeometryAssetLoader::EncodeMesh(mesh, payload, error)) {
                        test.Check(false, "Prepare stream tile");
                        return;
                    }
                    AssetContainer value;
                    value.id = id;
                    value.version = 1;
                    AssetType::TryParse("mesh", value.type);
                    value.payload = std::move(payload);
                    if (!WriteAssetContainer(value, container, error)) {
                        test.Check(false, "Encode stream tile container");
                        return;
                    }
                    metadata.tiles.push_back({x, z, lod, {id, 1, {}}, {}, mesh.mesh->GetData().localBounds});
                    files->emplace(id, std::move(container));
                }
        metadata.readDeferred = [files, reads](AssetId id, size_t limit, std::vector<std::byte>& out,
                                               std::string& error) {
            reads->fetch_add(1);
            const auto found = files->find(id);
            if (found == files->end() || found->second.size() > limit) {
                error = "Missing fixture tile";
                return false;
            }
            out = found->second;
            error.clear();
            return true;
        };
        JobSystem jobs;
        const bool running = jobs.Start({1, 16}, error);
        test.Check(running, "Start existing shared scheduler for terrain streaming");
        if (!running)
            return;
        AssetManager assets(jobs);
        test.Check(RegisterMaterialAssetLoaders(assets, error) && RegisterTerrainAssetLoader(assets, error),
                   "Register real terrain/material payload types");
        test.Check(assets.PublishBuiltin<MaterialInstance>(materialId, 1,
                                                           std::make_shared<const MaterialInstance>(),
                                                           sizeof(MaterialInstance), error) &&
                       assets.PublishBuiltin<TerrainAsset>(
                           terrainId, 1, std::make_shared<const TerrainAsset>(metadata),
                           sizeof(TerrainAsset) + metadata.tiles.size() * sizeof(TerrainTileRecord), error),
                   "Publish immutable metadata and isolated stream material owner");
        Scene scene("Terrain CPU stream fixture");
        const auto persistent = PersistentEntityId::FromWords(0x7465727261696e20, 1);
        test.Check(scene.QueueCreate(persistent, "Terrain", {}).IsSuccess() &&
                       scene.CommitPending().IsSuccess(),
                   "Create terrain author entity");
        TerrainComponent component;
        component.asset = AssetHandle<TerrainAsset>(terrainId);
        component.collisionEnabled = false;
        test.Check(SetTerrain(scene, scene.Resolve(persistent), component).IsSuccess() &&
                       scene.CommitPending().IsSuccess(),
                   "Terrain component uses ordinary scene safe point");
        TerrainService service;
        bool streamed = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (service.Draws().size() < 4 && std::chrono::steady_clock::now() < deadline && streamed) {
            streamed = service.Tick(scene, assets, jobs, {16, 20, 16}, nullptr, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        test.Check(streamed && service.Draws().size() == 4 && reads->load() >= 4,
                   "Workers stream real mesh containers into bounded owned terrain draws");
        const auto replacementId = AssetId::FromWords(0x7465727261696e00, 3),
                   missingMaterialId = AssetId::FromWords(0x7465727261696e00, 4);
        auto replacement = metadata;
        replacement.material = {missingMaterialId, 1, {}};
        TerrainComponent requested;
        requested.asset = AssetHandle<TerrainAsset>(replacementId);
        requested.collisionEnabled = false;
        TerrainComponent pendingComponent;
        test.Check(assets.PublishBuiltin<TerrainAsset>(replacementId, 1,
                                                       std::make_shared<const TerrainAsset>(replacement),
                                                       sizeof(TerrainAsset), error) &&
                       SetTerrain(scene, scene.Resolve(persistent), requested).IsSuccess() &&
                       scene.CommitPending().IsSuccess() &&
                       service.Tick(scene, assets, jobs, {16, 20, 16}, nullptr, error) &&
                       scene.GetWorld().Get(scene.Resolve(persistent), pendingComponent).IsSuccess() &&
                       pendingComponent.asset.id == replacementId &&
                       !pendingComponent.assetVersion.HasValue() && service.Draws().size() == 4,
                   "Waiting replacement material retains old draws without publishing the old terrain "
                   "version into the newly requested component");
        auto held = service.Draws();
        service.Stop();
        test.Check(!held.empty() && service.Budget().used > 0,
                   "Retired render snapshots retain their CPU budget reservation");
        held.clear();
        test.Check(service.Budget().used == 0, "Terrain CPU budget retires after the final frame snapshot");
        service.Stop();
        assets.Shutdown();
        jobs.Shutdown();
    }
} // namespace Catalpa::Tests
