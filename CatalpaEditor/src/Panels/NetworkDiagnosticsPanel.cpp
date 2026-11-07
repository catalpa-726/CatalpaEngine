#include "catpch.h"
#include "Panels/NetworkDiagnosticsPanel.h"
#include "Core/Application.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/PlatformServices.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Geometry/PrimitiveMesh.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/NetworkIdentityComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Renderer/RenderSurface.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t ServerSurface = 0x4341544e45540001ULL, ClientSurface = 0x4341544e45540002ULL;
    }
    void NetworkDiagnosticsPanel::Close(Application& app) {
        app.CloseEditorPreview(ServerSurface);
        app.CloseEditorPreview(ClientSurface);
        if (m_Server)
            m_Server->Stop();
        if (m_Client)
            m_Client->Stop();
        m_Server.reset();
        m_Client.reset();
        if (app.GetScenes().GetScene(m_ServerScene))
            (void)app.GetScenes().Unload(m_ServerScene);
        if (app.GetScenes().GetScene(m_ClientScene))
            (void)app.GetScenes().Unload(m_ClientScene);
        m_ServerScene = {};
        m_ClientScene = {};
        m_Visual = {};
        m_Object = {};
        m_NetworkId = {};
        m_Tick = 0;
        m_Generation = 0;
        m_LastTime = 0;
        m_Accumulator = 0;
        m_DroppedSeconds = 0;
    }
    bool NetworkDiagnosticsPanel::Spawn(Application& app, std::string& error) {
        auto* scene = app.GetScenes().GetScene(m_ServerScene);
        if (!scene) {
            error = "Loopback server Scene expired";
            return false;
        }
        if (scene->Resolve(m_Object).HasValue())
            return true;
        if (m_Generation == UINT64_MAX) {
            error = "Network demo generation exhausted";
            return false;
        }
        ++m_Generation;
        auto check = [&](WorldStatus status) {
            if (!status.IsSuccess())
                error = status.message;
            return status.IsSuccess();
        };
        if (!check(scene->QueueCreate(m_Object, "Replicated cube", {})) || !check(scene->CommitPending()))
            return false;
        const auto entity = scene->Resolve(m_Object);
        NetworkIdentityComponent identity;
        identity.identity = m_NetworkId;
        identity.generation = m_Generation;
        if (!check(QueueDynamicComponent(*scene, entity,
                                         [entity, identity](World& world) {
                                             return world.Add(entity, identity);
                                         })) ||
            !check(scene->QueueSetMeshRenderer(entity, m_Visual)) || !check(scene->CommitPending()))
            return false;
        error.clear();
        return true;
    }
    bool NetworkDiagnosticsPanel::Start(Application& app, std::string& error) try {
        Close(app);
        const auto check = [&](WorldStatus status) {
            if (!status.IsSuccess())
                error = status.message;
            return status.IsSuccess();
        };
        if (!GeneratePersistentEntityId(m_Object, error) || !GenerateStableId(m_NetworkId, error) ||
            !check(app.GetScenes().CreateScene("Loopback authority", m_ServerScene)) ||
            !check(app.GetScenes().CreateScene("Loopback replica", m_ClientScene)))
            return false;
        for (const auto handle : {m_ServerScene, m_ClientScene}) {
            auto* scene = app.GetScenes().GetScene(handle);
            PersistentEntityId camera;
            if (!GeneratePersistentEntityId(camera, error))
                return false;
            Math::TransformTRS view;
            view.translation = {0, .5f, 5};
            if (!check(scene->QueueCreate(camera, "Local preview camera", view)) ||
                !check(scene->CommitPending()))
                return false;
            CameraComponent lens;
            lens.lens.farPlane = 100;
            if (!check(scene->QueueSetCamera(scene->Resolve(camera), lens)) || !check(scene->CommitPending()))
                return false;
        }
        MeshData cube;
        auto status = CreateCubeMeshWithTangents(1, cube);
        if (status.IsSuccess())
            status = CreateMeshResource(cube, m_Visual.mesh);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        m_Visual.color = {.15f, .8f, .55f, 1};
        NetworkLimits limits;
        limits.maxQueuedBytes = size_t(m_Settings.faults.delayTicks + 6) * 64 * 1024 * 2;
        std::unique_ptr<INetworkTransport> server, client;
        if (!CreateSimulatedLoopbackPair(server, client, m_Settings.faults, error, limits))
            return false;
        StableId generation;
        if (!GenerateStableId(generation, error))
            return false;
        uint64_t session = 0;
        for (size_t i = 0; i < 8; ++i)
            session = (session << 8) | generation.bytes[i];
        if (!session)
            session = 1;
        ReplicationConfig left;
        left.limits = m_Settings.limits;
        ReplicationConfig right = left;
        right.localPeer = 2;
        right.remotePeer = 1;
        m_Server = std::make_unique<NetworkSystem>(std::make_unique<ReplicationService>(
            std::make_unique<NetworkSession>(std::move(server), session), left));
        m_Client = std::make_unique<NetworkSystem>(std::make_unique<ReplicationService>(
            std::make_unique<NetworkSession>(std::move(client), session), right));
        m_Server->SetSpawnRenderer(m_Visual);
        m_Client->SetSpawnRenderer(m_Visual);
        m_LastTime = ImGui::GetTime();
        return Spawn(app, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool NetworkDiagnosticsPanel::Tick(Application& app, double elapsed, std::string& error) {
        if (!m_Server || !m_Client)
            return true;
        const double step = 1.0 / m_Settings.tickRate;
        const double admitted = std::min(std::max(elapsed, 0.0), step * 8);
        m_DroppedSeconds += std::max(0.0, elapsed - admitted);
        m_Accumulator += admitted;
        for (size_t i = 0; i < 8 && m_Accumulator >= step; ++i) {
            if (m_Tick == UINT64_MAX) {
                error = "Loopback demo tick exhausted";
                return false;
            }
            ++m_Tick;
            auto* authority = app.GetScenes().GetScene(m_ServerScene);
            auto* replica = app.GetScenes().GetScene(m_ClientScene);
            if (!authority || !replica) {
                error = "Loopback demo World expired";
                return false;
            }
            if (m_Animate) {
                const auto entity = authority->Resolve(m_Object);
                if (entity.HasValue()) {
                    TransformComponent value;
                    auto status = authority->GetTransform(entity, value);
                    if (!status.IsSuccess()) {
                        error = status.message;
                        return false;
                    }
                    const float time = float(double(m_Tick) * step);
                    value.local.translation.x = std::sin(time) * 1.5f;
                    value.local.rotation = Math::Quat(Math::Vec3(0, time * .6f, 0));
                    status = authority->QueueSetLocal(entity, value.local);
                    if (status.IsSuccess())
                        status = authority->CommitPending();
                    if (!status.IsSuccess()) {
                        error = status.message;
                        return false;
                    }
                }
            }
            if (!m_Server->Tick(*authority, m_Tick, error) || !m_Client->Tick(*replica, m_Tick, error))
                return false;
            m_Accumulator -= step;
        }
        return true;
    }
    void NetworkDiagnosticsPanel::Draw(Application& app, const PanelText& text, std::string& error) {
        if (!m_LoadedSettings) {
            std::filesystem::path directory;
            if (!GetExecutableDirectory(directory, error))
                return;
            const auto file = directory / "Resources/Config/NetworkProfiles.json";
            std::error_code code;
            if (std::filesystem::is_regular_file(file, code)) {
                VirtualFileSystem files;
                VirtualPath path;
                std::vector<std::byte> bytes;
                if (!files.MountReadOnly("profiles", file.parent_path(), 0, error) ||
                    !VirtualPath::Parse("profiles:/NetworkProfiles.json", path, error) ||
                    !files.ReadFile(path, 64 * 1024, bytes, error) ||
                    !ReadLoopbackSettings({reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                                          m_Settings, error))
                    return;
            }
            m_LoadedSettings = true;
        }
        ImGui::TextWrapped(
            "%s", text("network.scope",
                       "Loopback only: two independent Worlds, explicit transform fields, spawn/despawn and "
                       "generations. Cross-machine transport and security are deferred.",
                       nullptr)
                      .c_str());
        ImGui::BeginDisabled(bool(m_Server));
        ImGui::InputScalar(text("network.delay", "Delay ticks", "DelayTicks").c_str(), ImGuiDataType_U32,
                           &m_Settings.faults.delayTicks);
        ImGui::InputScalar(text("network.drop", "Drop every N packets (0 off)", "DropEvery").c_str(),
                           ImGuiDataType_U32, &m_Settings.faults.dropEvery);
        ImGui::InputScalar(text("network.duplicate", "Duplicate every N (0 off)", "DuplicateEvery").c_str(),
                           ImGuiDataType_U32, &m_Settings.faults.duplicateEvery);
        ImGui::Checkbox(text("network.reorder", "Reorder due packets", "ReorderPackets").c_str(),
                        &m_Settings.faults.reverseDuePackets);
        ImGui::EndDisabled();
        if (!m_Server) {
            if (ImGui::Button(text("network.start", "Start loopback worlds", "StartLoopback").c_str())) {
                std::string settings;
                if (!WriteLoopbackSettings(m_Settings, settings, error) || !Start(app, error))
                    Close(app);
            }
            return;
        }
        if (ImGui::Button(text("network.stop", "Stop loopback worlds", "StopLoopback").c_str())) {
            Close(app);
            return;
        }
        ImGui::SameLine();
        ImGui::Checkbox(text("network.animate", "Animate authority", "AnimateAuthority").c_str(), &m_Animate);
        auto* authority = app.GetScenes().GetScene(m_ServerScene);
        if (authority) {
            if (authority->Resolve(m_Object).HasValue()) {
                if (ImGui::Button(
                        text("network.despawn", "Despawn authority object", "DespawnAuthority").c_str())) {
                    const auto status = authority->QueueDestroySubtree(authority->Resolve(m_Object));
                    if (!status.IsSuccess())
                        error = status.message;
                    else {
                        const auto committed = authority->CommitPending();
                        if (!committed.IsSuccess())
                            error = committed.message;
                    }
                }
            } else if (ImGui::Button(
                           text("network.respawn", "Respawn with new generation", "RespawnAuthority")
                               .c_str()))
                (void)Spawn(app, error);
        }
        const double now = ImGui::GetTime();
        if (!Tick(app, now - m_LastTime, error)) {
            Close(app);
            return;
        }
        m_LastTime = now;
        const auto a = m_Server->Stats(), b = m_Client->Stats();
        ImGui::Text("%s %llu | %s %llu | %s %llu | %s %llu | %s %zu",
                    text("network.tick", "Tick", nullptr).c_str(), static_cast<unsigned long long>(m_Tick),
                    text("network.authority_sent", "Authority sent", nullptr).c_str(),
                    static_cast<unsigned long long>(a.sent),
                    text("network.replica_received", "Replica received", nullptr).c_str(),
                    static_cast<unsigned long long>(b.received),
                    text("network.stale_duplicates", "Stale/duplicates", nullptr).c_str(),
                    static_cast<unsigned long long>(b.stale),
                    text("network.pending_deletes", "Pending deletes", nullptr).c_str(), a.pendingDespawn);
        ImGui::Text("%s %zu; %s %llu; %s %.3f %s",
                    text("network.replica_objects", "Replica objects", nullptr).c_str(), b.live,
                    text("network.rejected", "Rejected", nullptr).c_str(),
                    static_cast<unsigned long long>(b.rejected),
                    text("network.skipped_catch_up", "Skipped preview catch-up", nullptr).c_str(),
                    m_DroppedSeconds, text("network.seconds", "Seconds", nullptr).c_str());
        if (ImGui::BeginTable("LoopbackViews", 2, ImGuiTableFlags_Resizable)) {
            for (size_t i = 0; i < 2; ++i) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted((i ? text("network.replica", "Replica World", nullptr)
                                          : text("network.authority", "Authority World", nullptr))
                                           .c_str());
                const auto scene = i ? m_ClientScene : m_ServerScene;
                const uint64_t surfaceId = i ? ClientSurface : ServerSurface;
                const float width = std::clamp(ImGui::GetContentRegionAvail().x, 64.f, 768.f);
                const auto pixels = uint32_t(
                    std::clamp(width * ImGui::GetWindowViewport()->FramebufferScale.x, 64.f, 2048.f));
                if (!app.SetEditorPreview(scene, surfaceId, {pixels, pixels}, error))
                    continue;
                RenderSurfaceSnapshot surface;
                if (app.GetEditorPreview(surfaceId, surface) && surface.textureToken)
                    ImGui::Image(ImTextureRef(ImTextureID(surface.textureToken)), {width, width});
            }
            ImGui::EndTable();
        }
    }
} // namespace Catalpa::Editor
