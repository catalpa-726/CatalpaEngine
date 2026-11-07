#include "AssetEditors/TerrainEditor.h"
#include "Commands/GraphCommands.h"
#include "Core/Application.h"
#include "Assets/AssetManager.h"
#include "Terrain/TerrainAsset.h"
#include "Terrain/TerrainService.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Renderer/RenderSurface.h"
#include <imgui.h>

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t TerrainSurface = 0x4341545445525201ULL;
    }
    void TerrainEditor::Close(Application& app) {
        app.CloseEditorPreview(TerrainSurface);
        if (app.GetScenes().GetScene(m_Scene))
            (void)app.GetScenes().Unload(m_Scene);
        m_Scene = {};
        m_Document = {};
        m_Object = {};
        m_Camera = {};
        m_Version = 0;
    }
    bool TerrainEditor::Accepts(const AssetDocument& doc) const {
        return doc.TypeKey() == "terrain" || doc.AuthorPath().filename().string().ends_with(".terrain.json");
    }
    void TerrainEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text,
                             std::string& error) {
        const auto T = [&](const char* key, const char* fallback) {
            return text ? text(key, fallback, key) : std::string(fallback);
        };
        if (!doc.Source())
            return;
        TerrainSource value;
        if (!ReadTerrainSource(*doc.Source(), value, error))
            return;
        ImGui::TextWrapped("Read-only height source: %s", value.heightSource.c_str());
        ImGui::TextUnformatted(
            "Author edits change sampling settings; the original height bytes are never overwritten.");
        int width = int(value.settings.width), height = int(value.settings.height),
            quads = int(value.settings.tileQuads), lod = int(value.settings.lodCount);
        bool changed = ImGui::InputInt(T("terrain.width", "Width samples").c_str(), &width);
        changed |= ImGui::InputInt(T("terrain.height", "Height samples").c_str(), &height);
        changed |= ImGui::InputInt(T("terrain.quads", "Tile quads (power of two)").c_str(), &quads);
        changed |= ImGui::InputInt(T("terrain.lods", "LOD count").c_str(), &lod);
        value.settings.width = uint32_t(width);
        value.settings.height = uint32_t(height);
        value.settings.tileQuads = uint32_t(quads);
        value.settings.lodCount = uint32_t(lod);
        changed |= ImGui::DragFloat(T("terrain.spacing", "Sample spacing (m)").c_str(),
                                    &value.settings.spacing, 0.01f, 0.01f, 100);
        changed |= ImGui::DragFloat(T("terrain.scale", "Height scale").c_str(), &value.settings.heightScale,
                                    0.01f, -10000, 10000);
        changed |= ImGui::DragFloat(T("terrain.offset", "Height offset (m)").c_str(),
                                    &value.settings.heightOffset, 0.1f, -100000, 100000);
        changed |= ImGui::DragFloat(T("terrain.skirt", "Skirt depth (m)").c_str(), &value.settings.skirtDepth,
                                    0.1f, 0.01f, 100000);
        changed |= ImGui::Checkbox(T("terrain.build_collision", "Build finest-resolution collision").c_str(),
                                   &value.settings.buildCollision);
        changed |= ImGui::Checkbox(T("terrain.build_navigation", "Build associated navigation").c_str(),
                                   &value.settings.buildNavigation);
        if (changed) {
            std::string next;
            if (WriteTerrainSource(value, next, error))
                ChangeAuthorSource(doc, std::move(next), error);
        }
        ImGui::Separator();
        ImGui::TextUnformatted(
            "Build with Content Browser. Runtime: 64 visible tiles, 2 pending reads, 128 MiB CPU "
            "budget including retained frame snapshots.");
        if (!doc.Asset().HasValue())
            return;
        auto& assets = app.GetAssets();
        if (ImGui::Button(T("terrain.request", "Request cooked terrain metadata").c_str()))
            assets.Request(doc.Asset(), error);
        AssetVersionRef<TerrainAsset> asset;
        bool fallback = false;
        if (!assets.Acquire(doc.Asset(), asset, fallback, error) || fallback)
            return;
        const auto& terrain = *asset.Get();
        ImGui::Text("Version: %llu; products: %zu", static_cast<unsigned long long>(asset.GetVersion()),
                    terrain.tiles.size());
        ImGui::TextWrapped("Height SHA-256: %s", terrain.heightDigest.c_str());
        ImGui::Text("Derived collision: %s; navigation: %s",
                    terrain.settings.buildCollision ? "finest tile" : "disabled",
                    terrain.navigation.id.HasValue() ? "fixed cooked version" : "disabled");
        const auto origin = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(available.x, 32.0f), 200};
        ImGui::InvisibleButton("Terrain tile overview", size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(22, 27, 24, 255));
        const uint32_t xTiles = (terrain.settings.width - 1) / terrain.settings.tileQuads,
                       zTiles = (terrain.settings.height - 1) / terrain.settings.tileQuads;
        const float cell = std::min((size.x - 12) / xTiles, (size.y - 12) / zTiles);
        for (const auto& tile : terrain.tiles)
            if (tile.lod == 0) {
                ImVec2 low{origin.x + 6 + tile.x * cell, origin.y + 6 + tile.z * cell},
                    high{low.x + cell, low.y + cell};
                draw->AddRectFilled(low, high,
                                    IM_COL32(55 + tile.x * 31 % 60, 90 + tile.z * 17 % 80, 55, 255));
                draw->AddRect(low, high, IM_COL32(160, 185, 160, 255));
            }
        ImGui::TextUnformatted(
            "Tile overview uses shared integer grid sampling. Cross-LOD skirts are excluded from "
            "collision and navigation.");
        if (m_Document != doc.Id())
            Close(app);
        const auto check = [&](WorldStatus status) {
            if (!status.IsSuccess())
                error = status.message;
            return status.IsSuccess();
        };
        if (!m_Scene.HasValue()) {
            if (!check(app.GetScenes().CreateScene("Terrain preview", m_Scene)))
                return;
            m_Document = doc.Id();
            PersistentEntityId light;
            if (!GeneratePersistentEntityId(m_Object, error) ||
                !GeneratePersistentEntityId(m_Camera, error) || !GeneratePersistentEntityId(light, error))
                return;
            auto* scene = app.GetScenes().GetScene(m_Scene);
            Math::TransformTRS sunPose;
            sunPose.rotation = Math::Quat(Math::Vec3{-0.8f, 0.4f, 0});
            if (!scene || !check(scene->QueueCreate(m_Object, "Terrain", {})) ||
                !check(scene->QueueCreate(m_Camera, "Terrain preview camera", {})) ||
                !check(scene->QueueCreate(light, "Terrain preview light", sunPose)) ||
                !check(scene->CommitPending()))
                return;
            LightComponent sun;
            sun.parameters.intensity = 4;
            if (!check(scene->QueueSetLight(scene->Resolve(light), sun)) || !check(scene->CommitPending()))
                return;
        }
        auto* scene = app.GetScenes().GetScene(m_Scene);
        if (!scene) {
            error = "Terrain preview scene was unloaded";
            return;
        }
        if (m_Version != asset.GetVersion()) {
            const float spanX = (terrain.settings.width - 1) * terrain.settings.spacing,
                        spanZ = (terrain.settings.height - 1) * terrain.settings.spacing,
                        span = std::max({spanX, spanZ, 1.0f});
            float low = std::numeric_limits<float>::max(), high = -low;
            for (const auto& tile : terrain.tiles)
                if (tile.lod == 0) {
                    low = std::min(low, tile.bounds.minimum.y + terrain.settings.skirtDepth);
                    high = std::max(high, tile.bounds.maximum.y);
                }
            const Math::Vec3 target{spanX * 0.5f, (low + high) * 0.5f, spanZ * 0.5f};
            Math::TransformTRS camera;
            camera.translation = target + Math::Vec3{span * 0.8f, span * 0.8f, span * 0.8f};
            camera.rotation =
                glm::quatLookAtRH(glm::normalize(target - camera.translation), Math::Vec3{0, 1, 0});
            CameraComponent lens;
            lens.viewId = TerrainSurface;
            lens.historyRevision = asset.GetVersion();
            lens.lens.nearPlane = std::max(0.1f, span * 0.001f);
            lens.lens.farPlane = span * 10 + std::abs(high - low);
            TerrainComponent component;
            component.asset = AssetHandle<TerrainAsset>(asset.GetId());
            component.viewDistance = std::min(100000.0f, span * 8);
            component.lodDistance = std::max(1.0f, span / 4);
            component.collisionEnabled = false;
            if (!check(SetTerrain(*scene, scene->Resolve(m_Object), component)) ||
                !check(scene->QueueSetLocal(scene->Resolve(m_Camera), camera)) ||
                !check(scene->QueueSetCamera(scene->Resolve(m_Camera), lens)) ||
                !check(scene->CommitPending()))
                return;
            m_Version = asset.GetVersion();
        }
        const float previewWidth = std::clamp(ImGui::GetContentRegionAvail().x, 32.0f, 768.0f);
        const uint32_t pixels = uint32_t(
            std::clamp(previewWidth * ImGui::GetWindowViewport()->FramebufferScale.x, 1.0f, 2048.0f));
        if (!app.SetEditorPreview(m_Scene, TerrainSurface, {pixels, pixels}, error))
            return;
        RenderSurfaceSnapshot surface;
        if (app.GetEditorPreview(TerrainSurface, surface) && surface.textureToken)
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(surface.textureToken)),
                         {previewWidth, previewWidth});
        error.clear();
    }
} // namespace Catalpa::Editor
