#include "AssetEditors/PhysicsNavigationInspector.h"
#include "Documents/SceneDocument.h"
#include "Commands/EditorCommand.h"
#include "Physics/PhysicsService.h"
#include "Physics/PhysicsNavigationTerrainVersions.h"
#include "Navigation/NavigationService.h"
#include "Terrain/TerrainService.h"
#include "Scene/SceneDynamicComponents.h"
#include "Core/Serialization/AuthorJson.h"
#include <imgui.h>

namespace Catalpa::Editor {
    namespace {
        ArchiveValue Archive(const AuthorJson::Json& json) {
            std::unique_ptr<ArchiveValue> value;
            ArchiveError error;
            if (!JsonArchive::Parse(json.dump(), value, error))
                throw std::invalid_argument(error.ToString());
            return std::move(*value);
        }
        bool Capture(const Scene& scene, EntityId entity, DynamicComponentMap& out, std::string& error) {
            return CapturePhysicsComponents(scene, entity, out, error) &&
                   CaptureNavigationComponents(scene, entity, out, error) &&
                   CaptureTerrainComponents(scene, entity, out, error);
        }
        std::string Text(const std::optional<ArchiveValue>& value) {
            if (!value)
                return {};
            std::string text;
            ArchiveError error;
            if (!JsonArchive::Write(*value, text, error))
                throw std::invalid_argument(error.ToString());
            return text;
        }
        class ComponentCommand final : public EditorCommand {
          public:
            ComponentCommand(StableId owner, PersistentEntityId entity, std::string key,
                             std::optional<ArchiveValue> before, std::optional<ArchiveValue> next,
                             std::vector<AssetDependencyValue> versions)
                : EditorCommand(owner), m_Entity(entity), m_Key(std::move(key)), m_Before(std::move(before)),
                  m_Next(std::move(next)), m_Versions(std::move(versions)) {}
            bool Apply(Document& doc, std::string& error) override {
                return Run(doc, m_Before, m_Next, error);
            }
            bool Revert(Document& doc, std::string& error) override {
                return Run(doc, m_Next, m_Before, error);
            }

          private:
            bool Run(Document& document, const std::optional<ArchiveValue>& expected,
                     const std::optional<ArchiveValue>& next, std::string& error) try {
                auto* doc = dynamic_cast<SceneDocument*>(&document);
                auto* scene = doc ? doc->Resolve() : nullptr;
                if (!doc || doc->Id() != Owner() || !scene || doc->Playing() || !scene->CanEditNow() ||
                    !doc->CheckTarget(m_Entity, error)) {
                    if (error.empty())
                        error = "Physics author document is unavailable or playing";
                    return false;
                }
                const auto entity = scene->Resolve(m_Entity);
                DynamicComponentMap actual;
                if (!Capture(*scene, entity, actual, error))
                    return false;
                std::optional<ArchiveValue> current;
                if (auto i = actual.find(m_Key); i != actual.end())
                    current = i->second;
                if (Text(current) != Text(expected)) {
                    error = "Physics/navigation component changed outside the undo command";
                    return false;
                }
                if (next) {
                    DynamicComponentMap candidate = actual;
                    candidate[m_Key] = *next;
                    if (!ValidatePhysicsComponents(candidate, error) ||
                        !ValidateNavigationComponents(candidate, error) ||
                        !ValidateTerrainComponents(candidate, error))
                        return false;
                    const DynamicComponentMap one{{m_Key, *next}};
                    const auto key = m_Key;
                    const auto versions = m_Versions;
                    const auto queued = QueueDynamicComponent(
                        *scene, entity, [entity, key, one, versions](World& world) -> WorldStatus {
                            const auto transaction = [&]<class T>() -> WorldStatus {
                                std::optional<T> previous;
                                if (world.Has<T>(entity)) {
                                    previous.emplace();
                                    const auto status = world.Get(entity, *previous);
                                    if (!status.IsSuccess())
                                        return status;
                                }
                                std::string error;
                                const bool restored =
                                    key == "terrain" ? RestoreTerrainComponents(world, entity, one, error)
                                    : key == "navigation_agent"
                                        ? RestoreNavigationComponents(world, entity, one, error)
                                        : RestorePhysicsComponents(world, entity, one, error);
                                if (restored &&
                                    BindPhysicsNavigationTerrainVersions(world, entity, versions, error))
                                    return {};
                                // 组件值只包含固定字段/shared owner；Set/Remove回滚不分配新载荷。
                                WorldStatus rollback;
                                if (previous)
                                    rollback = world.Set(entity, *previous);
                                else if (world.Has<T>(entity))
                                    rollback = world.Remove<T>(entity);
                                if (!rollback.IsSuccess())
                                    return {WorldError::CommandFailed, "Component rollback failed"};
                                return {WorldError::InvalidArgument,
                                        "Component values or fixed asset versions are invalid"};
                            };
                            if (key == "rigid_body")
                                return transaction.template operator()<RigidBodyComponent>();
                            if (key == "collider")
                                return transaction.template operator()<ColliderComponent>();
                            if (key == "character_controller")
                                return transaction.template operator()<CharacterControllerComponent>();
                            if (key == "navigation_agent")
                                return transaction.template operator()<NavigationAgentComponent>();
                            return transaction.template operator()<TerrainComponent>();
                        });
                    if (!queued.IsSuccess()) {
                        error = queued.message;
                        return false;
                    }
                } else {
                    const auto key = m_Key;
                    const auto status =
                        QueueDynamicComponent(*scene, entity, [entity, key](World& world) -> WorldStatus {
                            if (key == "rigid_body")
                                return world.Remove<RigidBodyComponent>(entity);
                            if (key == "collider")
                                return world.Remove<ColliderComponent>(entity);
                            if (key == "character_controller")
                                return world.Remove<CharacterControllerComponent>(entity);
                            if (key == "terrain")
                                return world.Remove<TerrainComponent>(entity);
                            return world.Remove<NavigationAgentComponent>(entity);
                        });
                    if (!status.IsSuccess()) {
                        error = status.message;
                        return false;
                    }
                }

                const auto committed = scene->CommitPending();
                if (!committed.IsSuccess()) {
                    error = committed.message;
                    return false;
                }
                error.clear();
                return true;
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
            PersistentEntityId m_Entity;
            std::string m_Key;
            std::optional<ArchiveValue> m_Before, m_Next;
            std::vector<AssetDependencyValue> m_Versions;
        };
        bool Edit(SceneDocument& doc, EntityId entity, std::string key, std::optional<ArchiveValue> next,
                  std::string& error) {
            auto* scene = doc.Resolve();
            if (!scene)
                return false;
            DynamicComponentMap values;
            if (!Capture(*scene, entity, values, error))
                return false;
            std::optional<ArchiveValue> before;
            if (auto i = values.find(key); i != values.end())
                before = i->second;
            std::vector<AssetDependencyValue> versions;
            if (!CollectPhysicsNavigationTerrainVersions(scene->GetWorld(), entity, versions, error))
                return false;
            return doc.History().Execute(
                doc,
                std::make_unique<ComponentCommand>(doc.Id(), scene->MakeReference(entity).persistent,
                                                   std::move(key), std::move(before), std::move(next),
                                                   std::move(versions)),
                error);
        }
        bool AssetInput(const char* label, AssetId& id, std::string& error) {
            struct Draft {
                AssetId original;
                std::array<char, 64> text{};
                bool initialized = false;
            };
            static std::map<ImGuiID, Draft> drafts;
            const auto key = ImGui::GetID(label);
            if (!drafts.contains(key) && drafts.size() >= 256)
                drafts.clear();
            auto& draft = drafts[key];
            if (!draft.initialized || draft.original != id) {
                draft = {};
                draft.original = id;
                draft.initialized = true;
                const auto text = id.HasValue() ? id.ToString() : std::string{};
                std::copy(text.begin(), text.end(), draft.text.begin());
            }
            if (!ImGui::InputText(label, draft.text.data(), draft.text.size(),
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                return false;
            if (!draft.text[0]) {
                id = {};
                draft.original = id;
                return true;
            }
            AssetId next;
            if (!AssetId::TryParse(draft.text.data(), next)) {
                error = "Expected a stable asset UUID";
                return false;
            }
            id = next;
            draft.original = id;
            return true;
        }
    } // namespace
    void DrawPhysicsNavigationInspector(SceneDocument& doc, EntityId entity, std::string& error,
                                        const PanelText& text) try {
        const auto T = [&](const char* key, const char* fallback) {
            return text ? text(key, fallback, key) : std::string(fallback);
        };
        auto* scene = doc.Resolve();
        if (!scene || !scene->GetWorld().IsAlive(entity))
            return;
        const auto& w = scene->GetWorld();
        struct DisabledScope {
            explicit DisabledScope(bool value) {
                ImGui::BeginDisabled(value);
            }
            ~DisabledScope() {
                ImGui::EndDisabled();
            }
        } disabled(doc.Playing());
        if (w.Has<RigidBodyComponent>(entity)) {
            RigidBodyComponent v;
            if (!w.Get(entity, v).IsSuccess()) {
                return;
            }
            if (ImGui::CollapsingHeader(T("physics.body", "Rigid body").c_str())) {
                int motion = int(v.motion);
                bool changed = ImGui::Combo(T("physics.motion", "Motion").c_str(), &motion,
                                            "Static\0Kinematic\0Dynamic\0");
                v.motion = PhysicsMotion(motion);
                changed |= ImGui::Checkbox(T("physics.body_enabled", "Body enabled").c_str(), &v.enabled);
                changed |=
                    ImGui::DragFloat(T("physics.mass", "Mass (kg)").c_str(), &v.massKg, 0.1f, 0.001f, 1e8f);
                changed |= ImGui::DragFloat(T("physics.gravity", "Gravity factor").c_str(), &v.gravityFactor,
                                            0.01f, 0, 100);
                changed |= ImGui::DragFloat3(T("physics.initial_velocity", "Initial velocity (m/s)").c_str(),
                                             &v.initialVelocity.x, 0.1f);
                int group = v.collisionGroup;
                changed |=
                    ImGui::SliderInt(T("physics.body_group", "Body collision group").c_str(), &group, 0, 15);
                v.collisionGroup = uint8_t(group);
                if (changed)
                    Edit(doc, entity, "rigid_body",
                         Archive(AuthorJson::Json{{"motion", motion},
                                                  {"massKg", v.massKg},
                                                  {"gravityFactor", v.gravityFactor},
                                                  {"initialVelocity", AuthorJson::Array(v.initialVelocity)},
                                                  {"collisionGroup", group},
                                                  {"teleportRevision", v.teleportRevision},
                                                  {"enabled", v.enabled}}),
                         error);
                if (ImGui::Button(T("physics.remove_body", "Remove rigid body").c_str()))
                    Edit(doc, entity, "rigid_body", {}, error);
            }
        } else if (!w.Has<CharacterControllerComponent>(entity) &&
                   ImGui::Button(T("physics.add_body", "Add rigid body").c_str()))
            Edit(doc, entity, "rigid_body",
                 Archive(AuthorJson::Json{{"motion", 0},
                                          {"massKg", 1},
                                          {"gravityFactor", 1},
                                          {"initialVelocity", {0, 0, 0}},
                                          {"collisionGroup", 0},
                                          {"teleportRevision", 0},
                                          {"enabled", true}}),
                 error);
        if (w.Has<ColliderComponent>(entity)) {
            ColliderComponent v;
            if (!w.Get(entity, v).IsSuccess()) {
                return;
            }
            if (ImGui::CollapsingHeader(T("physics.collider", "Collider").c_str())) {
                bool changed =
                    AssetInput(T("physics.shape_asset", "Collision shape asset").c_str(), v.shape.id, error);
                changed |=
                    AssetInput(T("physics.material_asset", "Physics material asset (optional)").c_str(),
                               v.material.id, error);
                changed |= ImGui::Checkbox(T("physics.sensor", "Sensor / trigger").c_str(), &v.sensor);
                if (changed)
                    Edit(doc, entity, "collider",
                         Archive(AuthorJson::Json{
                             {"shape", v.shape.id.ToString()},
                             {"material", v.material.HasValue() ? v.material.id.ToString() : ""},
                             {"sensor", v.sensor}}),
                         error);
                if (ImGui::Button(T("physics.remove_collider", "Remove collider").c_str()))
                    Edit(doc, entity, "collider", {}, error);
            }
        } else {
            static AssetId shape;
            AssetInput(T("physics.new_shape", "Shape UUID for new collider").c_str(), shape, error);
            if (shape.HasValue() && ImGui::Button(T("physics.add_collider", "Add collider").c_str()))
                Edit(doc, entity, "collider",
                     Archive(
                         AuthorJson::Json{{"shape", shape.ToString()}, {"material", ""}, {"sensor", false}}),
                     error);
        }
        if (w.Has<CharacterControllerComponent>(entity)) {
            CharacterControllerComponent v;
            if (!w.Get(entity, v).IsSuccess()) {
                return;
            }
            if (ImGui::CollapsingHeader(T("physics.character", "Character controller").c_str())) {
                auto& s = v.settings;
                bool changed =
                    ImGui::Checkbox(T("physics.character_enabled", "Character enabled").c_str(), &v.enabled);
                changed |= ImGui::DragFloat(T("physics.radius", "Capsule radius (m)").c_str(), &s.radius,
                                            0.01f, 0.01f, 100);
                changed |=
                    ImGui::DragFloat(T("physics.half_height", "Capsule cylinder half-height (m)").c_str(),
                                     &s.halfHeight, 0.01f, 0.01f, 100);
                float degrees = s.maxSlopeRadians * 57.2957795f;
                changed |= ImGui::SliderFloat(T("physics.slope", "Maximum slope (degrees)").c_str(), &degrees,
                                              0, 89);
                s.maxSlopeRadians = degrees * 0.0174532925f;
                changed |= ImGui::DragFloat(T("physics.stair", "Stair height (m)").c_str(), &s.stepHeight,
                                            0.01f, 0, 10);
                changed |= ImGui::DragFloat(T("physics.ground_snap", "Ground snap (m)").c_str(),
                                            &s.snapDistance, 0.01f, 0, 10);
                changed |= ImGui::DragFloat(T("physics.character_mass", "Character mass (kg)").c_str(),
                                            &s.massKg, 0.1f, 1, 10000);
                int group = s.collisionGroup;
                changed |= ImGui::SliderInt(T("physics.character_group", "Character collision group").c_str(),
                                            &group, 0, 15);
                s.collisionGroup = uint8_t(group);
                if (changed)
                    Edit(doc, entity, "character_controller",
                         Archive(AuthorJson::Json{{"radius", s.radius},
                                                  {"halfHeight", s.halfHeight},
                                                  {"slopeRadians", s.maxSlopeRadians},
                                                  {"stepHeight", s.stepHeight},
                                                  {"snapDistance", s.snapDistance},
                                                  {"massKg", s.massKg},
                                                  {"collisionGroup", group},
                                                  {"desiredVelocity", AuthorJson::Array(v.desiredVelocity)},
                                                  {"teleportRevision", v.teleportRevision},
                                                  {"enabled", v.enabled}}),
                         error);
                if (ImGui::Button(T("physics.remove_character", "Remove character controller").c_str()))
                    Edit(doc, entity, "character_controller", {}, error);
            }
        } else if (!w.Has<RigidBodyComponent>(entity) &&
                   ImGui::Button(T("physics.add_character", "Add character controller").c_str()))
            Edit(doc, entity, "character_controller",
                 Archive(AuthorJson::Json{{"radius", 0.3},
                                          {"halfHeight", 0.6},
                                          {"slopeRadians", 0.78539816},
                                          {"stepHeight", 0.3},
                                          {"snapDistance", 0.3},
                                          {"massKg", 70},
                                          {"collisionGroup", 0},
                                          {"desiredVelocity", {0, 0, 0}},
                                          {"teleportRevision", 0},
                                          {"enabled", true}}),
                 error);
        if (w.Has<NavigationAgentComponent>(entity)) {
            NavigationAgentComponent v;
            if (!w.Get(entity, v).IsSuccess()) {
                return;
            }
            if (ImGui::CollapsingHeader(T("navigation.agent", "Navigation agent").c_str())) {
                bool changed =
                    AssetInput(T("navigation.mesh", "Navigation mesh UUID").c_str(), v.mesh.id, error);
                changed |= ImGui::InputFloat3(T("navigation.destination", "Destination (m)").c_str(),
                                              &v.destination.x);
                changed |= ImGui::DragFloat(T("navigation.speed", "Agent speed (m/s)").c_str(), &v.speed,
                                            0.1f, 0, 100);
                changed |= ImGui::DragFloat(T("navigation.arrival", "Arrival distance (m)").c_str(),
                                            &v.stoppingDistance, 0.01f, 0.01f, 100);
                changed |= ImGui::Checkbox(T("navigation.enabled", "Navigation enabled").c_str(), &v.enabled);
                if (changed && v.requestRevision < UINT64_MAX) {
                    ++v.requestRevision;
                    Edit(doc, entity, "navigation_agent",
                         Archive(AuthorJson::Json{{"mesh", v.mesh.id.ToString()},
                                                  {"destination", AuthorJson::Array(v.destination)},
                                                  {"speed", v.speed},
                                                  {"stoppingDistance", v.stoppingDistance},
                                                  {"requestRevision", v.requestRevision},
                                                  {"enabled", v.enabled}}),
                         error);
                }
                if (ImGui::Button(T("navigation.remove_agent", "Remove navigation agent").c_str()))
                    Edit(doc, entity, "navigation_agent", {}, error);
            }
        } else {
            static AssetId mesh;
            AssetInput(T("navigation.new_mesh", "Mesh UUID for new navigation agent").c_str(), mesh, error);
            if (mesh.HasValue() && ImGui::Button(T("navigation.add_agent", "Add navigation agent").c_str()))
                Edit(doc, entity, "navigation_agent",
                     Archive(AuthorJson::Json{{"mesh", mesh.ToString()},
                                              {"destination", {0, 0, 0}},
                                              {"speed", 2},
                                              {"stoppingDistance", 0.1},
                                              {"requestRevision", 1},
                                              {"enabled", true}}),
                     error);
        }
        if (w.Has<TerrainComponent>(entity)) {
            TerrainComponent value;
            if (!w.Get(entity, value).IsSuccess())
                return;
            if (ImGui::CollapsingHeader(T("terrain.component", "Terrain").c_str())) {
                bool changed =
                    AssetInput(T("terrain.asset", "Terrain asset UUID").c_str(), value.asset.id, error);
                changed |= ImGui::Checkbox(T("terrain.enabled", "Terrain enabled").c_str(), &value.enabled);
                changed |= ImGui::Checkbox(T("terrain.collision", "Terrain collision").c_str(),
                                           &value.collisionEnabled);
                changed |= ImGui::DragFloat(T("terrain.view_distance", "View distance (m)").c_str(),
                                            &value.viewDistance, 1, 1, 100000);
                changed |= ImGui::DragFloat(T("terrain.lod_distance", "LOD distance (m)").c_str(),
                                            &value.lodDistance, 1, 1, 100000);
                if (changed)
                    Edit(doc, entity, "terrain",
                         Archive(AuthorJson::Json{{"asset", value.asset.id.ToString()},
                                                  {"viewDistance", value.viewDistance},
                                                  {"lodDistance", value.lodDistance},
                                                  {"enabled", value.enabled},
                                                  {"collisionEnabled", value.collisionEnabled}}),
                         error);
                if (ImGui::Button(T("terrain.remove", "Remove terrain").c_str()))
                    Edit(doc, entity, "terrain", {}, error);
            }
        } else {
            static AssetId terrain;
            AssetInput(T("terrain.new_asset", "UUID for new terrain").c_str(), terrain, error);
            if (terrain.HasValue() && ImGui::Button(T("terrain.add", "Add terrain").c_str()))
                Edit(doc, entity, "terrain",
                     Archive(AuthorJson::Json{{"asset", terrain.ToString()},
                                              {"viewDistance", 250},
                                              {"lodDistance", 50},
                                              {"enabled", true},
                                              {"collisionEnabled", true}}),
                     error);
        }
    } catch (const std::exception& e) {
        error = e.what();
    }
} // namespace Catalpa::Editor
