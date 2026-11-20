#include "Panels/EditorText.h"
#include "AssetEditors/PhysicsNavigationInspector.h"

#include "Documents/SceneDocument.h"
#include "Assets/BuiltinAssetIds.h"
#include "Assets/AssetManager.h"
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
			    std::vector<AssetDependencyValue> beforeVersions, std::vector<AssetDependencyValue> afterVersions)
			    : EditorCommand(owner), m_Entity(entity), m_Key(std::move(key)), m_Before(std::move(before)),
			      m_Next(std::move(next)), m_BeforeVersions(std::move(beforeVersions)),
			      m_AfterVersions(std::move(afterVersions)) {}

			bool Apply(Document& doc, std::string& error) override {
				return Run(doc, m_Before, m_Next, m_AfterVersions, error);
			}

			bool Revert(Document& doc, std::string& error) override {
				return Run(doc, m_Next, m_Before, m_BeforeVersions, error);
			}

		private:
			/**
			 * @brief 核对目标实体和当前组件快照后写入新的组件数据。
			 *
			 * @param[in] document 持有目标实体的场景文档，执行前核对其中的物理或导航组件快照。
			 * @param[in] expected 用于比较的 std::optional<ArchiveValue>。
			 * @param[in] next 下一状态的 std::optional<ArchiveValue>。
			 * @param[out] error 失败时接收原因说明。
			 *
			 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
			 */
			bool Run(Document& document, const std::optional<ArchiveValue>& expected,
			    const std::optional<ArchiveValue>& next, const std::vector<AssetDependencyValue>& versions,
			    std::string& error) try {
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
					    !ValidateNavigationComponents(candidate, error) || !ValidateTerrainComponents(candidate, error))
						return false;
					const DynamicComponentMap one{{m_Key, *next}};
					const auto key = m_Key;
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
							    const bool restored = key == "terrain"
							        ? RestoreTerrainComponents(world, entity, one, error)
							        : key == "navigation_agent" ? RestoreNavigationComponents(world, entity, one, error)
							                                    : RestorePhysicsComponents(world, entity, one, error);
							    if (restored && BindPhysicsNavigationTerrainVersions(world, entity, versions, error))
								    return {};
							    /* 组件值只包含固定字段和共享引用，回滚 Set 或 Remove 时无须分配新载荷。 */
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
			std::vector<AssetDependencyValue> m_BeforeVersions, m_AfterVersions;
		};

		bool Edit(SceneDocument& doc, EntityId entity, std::string key, std::optional<ArchiveValue> next,
		    std::string& error, const AssetManager* assets = nullptr, AssetId picked = {}) {
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
			auto after = versions;
			if (picked.HasValue()) {
				/* 替换引用时分别固定前后版本，以便撤销恢复原地形及其材质。 */
				AssetDependencyValue version;
				if (!assets || !assets->GetDependencySnapshot(picked, version, error))
					return false;
				const auto keep = [&](const AssetDependencyValue& pin) {
					std::erase_if(after, [&](const auto& old) { return old.id == pin.id; });
					after.push_back(pin);
				};
				keep(version);
				if (version.cppType == typeid(TerrainAsset)) {
					const auto material = std::static_pointer_cast<const TerrainAsset>(version.value)->material.id;
					if (material.HasValue()) {
						if (!assets->GetDependencySnapshot(material, version, error))
							return false;
						keep(version);
					}
				}
			}
			return doc.History().Execute(doc,
			    std::make_unique<ComponentCommand>(doc.Id(), scene->MakeReference(entity).persistent, std::move(key),
			        std::move(before), std::move(next), std::move(versions), std::move(after)),
			    error);
		}

	} /* 命名空间 */
	void DrawPhysicsNavigationInspector(SceneDocument& doc, EntityId entity, std::string& error, const PanelText& text,
	    std::string_view addType, const ComponentAssetPicker& pick, AssetManager& assets, InspectorSection reveal) try {
		const auto T = [&](const char* key, const char* fallback) {
			return text ? text(key, fallback, key) : std::string(fallback);
		};
		auto* scene = doc.Resolve();
		if (!scene || !scene->GetWorld().IsAlive(entity))
			return;
		const auto& w = scene->GetWorld();
		struct DisabledScope {
			explicit DisabledScope(bool value) { ImGui::BeginDisabled(value); }

			~DisabledScope() { ImGui::EndDisabled(); }
		} disabled(doc.Playing());
		if (w.Has<RigidBodyComponent>(entity)) {
			RigidBodyComponent v;
			if (!w.Get(entity, v).IsSuccess()) {
				return;
			}
			if (reveal.Header("physics.body", T("physics.body", "Rigid body").c_str())) {
				int motion = int(v.motion);
				bool changed = ImGui::Combo(T("physics.motion", "Motion").c_str(), &motion,
				    UiChoices({UiText("choice.static", "Static"), UiText("choice.kinematic", "Kinematic"),
				                  UiText("choice.dynamic", "Dynamic")})
				        .c_str());
				v.motion = PhysicsMotion(motion);
				changed |= ImGui::Checkbox(T("physics.body_enabled", "Body enabled").c_str(), &v.enabled);
				changed |= ImGui::DragFloat(T("physics.mass", "Mass (kg)").c_str(), &v.massKg, 0.1f, 0.001f, 1e8f);
				changed |=
				    ImGui::DragFloat(T("physics.gravity", "Gravity factor").c_str(), &v.gravityFactor, 0.01f, 0, 100);
				changed |= ImGui::DragFloat3(
				    T("physics.initial_velocity", "Initial velocity (m/s)").c_str(), &v.initialVelocity.x, 0.1f);
				int group = v.collisionGroup;
				changed |= ImGui::SliderInt(T("physics.body_group", "Body collision group").c_str(), &group, 0, 15);
				v.collisionGroup = uint8_t(group);
				if (changed)
					Edit(doc, entity, "rigid_body",
					    Archive(AuthorJson::Json{{"motion", motion}, {"massKg", v.massKg},
					        {"gravityFactor", v.gravityFactor},
					        {"initialVelocity", AuthorJson::Array(v.initialVelocity)}, {"collisionGroup", group},
					        {"teleportRevision", v.teleportRevision}, {"enabled", v.enabled}}),
					    error);
				if (ImGui::Button(T("physics.remove_body", "Remove rigid body").c_str()))
					Edit(doc, entity, "rigid_body", {}, error);
			}
		} else if (addType == "RigidBodyComponent" && !w.Has<CharacterControllerComponent>(entity))
			Edit(doc, entity, "rigid_body",
			    Archive(AuthorJson::Json{{"motion", int(PhysicsMotion::Dynamic)}, {"massKg", 1.f}, {"gravityFactor", 1.f},
			        {"initialVelocity", AuthorJson::Array(glm::vec3(0))}, {"collisionGroup", 0},
			        {"teleportRevision", 0}, {"enabled", true}}),
			    error);
		if (w.Has<ColliderComponent>(entity)) {
			ColliderComponent v;
			if (!w.Get(entity, v).IsSuccess()) {
				return;
			}
			if (reveal.Header("physics.collider", T("physics.collider", "Collider").c_str())) {
				const bool shapeChanged =
				    pick(T("physics.shape_asset", "Collision shape").c_str(), "collision_shape", v.shape.id);
				const bool materialChanged = pick(T("physics.material_asset", "Physics material (optional)").c_str(),
				    "physics_material", v.material.id);
				bool changed = shapeChanged || materialChanged;
				changed |= ImGui::Checkbox(T("physics.sensor", "Sensor / trigger").c_str(), &v.sensor);
				if (changed)
					Edit(doc, entity, "collider",
					    Archive(AuthorJson::Json{{"shape", v.shape.id.ToString()},
					        {"material", v.material.HasValue() ? v.material.id.ToString() : ""}, {"sensor", v.sensor}}),
					    error, &assets,
					    shapeChanged          ? v.shape.id
					        : materialChanged ? v.material.id
					                          : AssetId{});
				if (ImGui::Button(T("physics.remove_collider", "Remove collider").c_str()))
					Edit(doc, entity, "collider", {}, error);
			}
		} else if (addType == "ColliderComponent") {
			const auto shape = BuiltinAssetIds::BoxCollisionShape;
			Edit(doc, entity, "collider",
			    Archive(AuthorJson::Json{{"shape", shape.ToString()}, {"material", ""}, {"sensor", false}}), error,
			    &assets, shape);
		}
		if (w.Has<CharacterControllerComponent>(entity)) {
			CharacterControllerComponent v;
			if (!w.Get(entity, v).IsSuccess()) {
				return;
			}
			if (reveal.Header("physics.character", T("physics.character", "Character controller").c_str())) {
				auto& s = v.settings;
				bool changed = ImGui::Checkbox(T("physics.character_enabled", "Character enabled").c_str(), &v.enabled);
				changed |=
				    ImGui::DragFloat(T("physics.radius", "Capsule radius (m)").c_str(), &s.radius, 0.01f, 0.01f, 100);
				changed |= ImGui::DragFloat(T("physics.half_height", "Capsule cylinder half-height (m)").c_str(),
				    &s.halfHeight, 0.01f, 0.01f, 100);
				float degrees = s.maxSlopeRadians * 57.2957795f;
				changed |= ImGui::SliderFloat(T("physics.slope", "Maximum slope (degrees)").c_str(), &degrees, 0, 89);
				s.maxSlopeRadians = degrees * 0.0174532925f;
				changed |=
				    ImGui::DragFloat(T("physics.stair", "Stair height (m)").c_str(), &s.stepHeight, 0.01f, 0, 10);
				changed |= ImGui::DragFloat(
				    T("physics.ground_snap", "Ground snap (m)").c_str(), &s.snapDistance, 0.01f, 0, 10);
				changed |= ImGui::DragFloat(
				    T("physics.character_mass", "Character mass (kg)").c_str(), &s.massKg, 0.1f, 1, 10000);
				int group = s.collisionGroup;
				changed |=
				    ImGui::SliderInt(T("physics.character_group", "Character collision group").c_str(), &group, 0, 15);
				s.collisionGroup = uint8_t(group);
				if (changed)
					Edit(doc, entity, "character_controller",
					    Archive(AuthorJson::Json{{"radius", s.radius}, {"halfHeight", s.halfHeight},
					        {"slopeRadians", s.maxSlopeRadians}, {"stepHeight", s.stepHeight},
					        {"snapDistance", s.snapDistance}, {"massKg", s.massKg}, {"collisionGroup", group},
					        {"desiredVelocity", AuthorJson::Array(v.desiredVelocity)},
					        {"teleportRevision", v.teleportRevision}, {"enabled", v.enabled}}),
					    error);
				if (ImGui::Button(T("physics.remove_character", "Remove character controller").c_str()))
					Edit(doc, entity, "character_controller", {}, error);
			}
		} else if (addType == "CharacterControllerComponent" && !w.Has<RigidBodyComponent>(entity))
			Edit(doc, entity, "character_controller",
			    Archive(AuthorJson::Json{{"radius", 0.3f}, {"halfHeight", 0.6f}, {"slopeRadians", 0.78539816f},
			        {"stepHeight", 0.3f}, {"snapDistance", 0.3f}, {"massKg", 70.f}, {"collisionGroup", 0},
			        {"desiredVelocity", AuthorJson::Array(glm::vec3(0))}, {"teleportRevision", 0}, {"enabled", true}}),
			    error);
		if (w.Has<NavigationAgentComponent>(entity)) {
			NavigationAgentComponent v;
			if (!w.Get(entity, v).IsSuccess()) {
				return;
			}
			if (reveal.Header("navigation.agent", T("navigation.agent", "Navigation agent").c_str())) {
				const bool meshChanged = pick(T("navigation.mesh", "Navigation mesh").c_str(), "navmesh", v.mesh.id);
				bool changed = meshChanged;
				changed |= ImGui::InputFloat3(T("navigation.destination", "Destination (m)").c_str(), &v.destination.x);
				changed |= ImGui::DragFloat(T("navigation.speed", "Agent speed (m/s)").c_str(), &v.speed, 0.1f, 0, 100);
				changed |= ImGui::DragFloat(
				    T("navigation.arrival", "Arrival distance (m)").c_str(), &v.stoppingDistance, 0.01f, 0.01f, 100);
				changed |= ImGui::Checkbox(T("navigation.enabled", "Navigation enabled").c_str(), &v.enabled);
				if (changed && v.requestRevision < UINT64_MAX) {
					++v.requestRevision;
					Edit(doc, entity, "navigation_agent",
					    Archive(AuthorJson::Json{{"mesh", v.mesh.id.ToString()},
					        {"destination", AuthorJson::Array(v.destination)}, {"speed", v.speed},
					        {"stoppingDistance", v.stoppingDistance}, {"requestRevision", v.requestRevision},
					        {"enabled", v.enabled}}),
					    error, &assets, meshChanged ? v.mesh.id : AssetId{});
				}
				if (ImGui::Button(T("navigation.remove_agent", "Remove navigation agent").c_str()))
					Edit(doc, entity, "navigation_agent", {}, error);
			}
		} else if (addType == "NavigationAgentComponent") {
			AssetId mesh;
			if (pick(T("navigation.mesh", "Navigation mesh").c_str(), "navmesh", mesh))
				Edit(doc, entity, "navigation_agent",
				    Archive(
				        AuthorJson::Json{{"mesh", mesh.ToString()}, {"destination", AuthorJson::Array(glm::vec3(0))},
				            {"speed", 2.f}, {"stoppingDistance", 0.1f}, {"requestRevision", 1}, {"enabled", true}}),
				    error, &assets, mesh);
		}
		if (w.Has<TerrainComponent>(entity)) {
			TerrainComponent value;
			if (!w.Get(entity, value).IsSuccess())
				return;
			if (reveal.Header("terrain.component", T("terrain.component", "Terrain").c_str())) {
				const bool terrainChanged =
				    pick(T("terrain.asset", "Terrain asset").c_str(), "terrain", value.asset.id);
				bool changed = terrainChanged;
				changed |= ImGui::Checkbox(T("terrain.enabled", "Terrain enabled").c_str(), &value.enabled);
				changed |=
				    ImGui::Checkbox(T("terrain.collision", "Terrain collision").c_str(), &value.collisionEnabled);
				changed |= ImGui::DragFloat(
				    T("terrain.view_distance", "View distance (m)").c_str(), &value.viewDistance, 1, 1, 100000);
				changed |= ImGui::DragFloat(
				    T("terrain.lod_distance", "LOD distance (m)").c_str(), &value.lodDistance, 1, 1, 100000);
				if (changed)
					Edit(doc, entity, "terrain",
					    Archive(AuthorJson::Json{{"asset", value.asset.id.ToString()},
					        {"viewDistance", value.viewDistance}, {"lodDistance", value.lodDistance},
					        {"enabled", value.enabled}, {"collisionEnabled", value.collisionEnabled}}),
					    error, &assets, terrainChanged ? value.asset.id : AssetId{});
				if (ImGui::Button(T("terrain.remove", "Remove terrain").c_str()))
					Edit(doc, entity, "terrain", {}, error);
			}
		} else if (addType == "TerrainComponent") {
			AssetId terrain;
			if (pick(T("terrain.asset", "Terrain asset").c_str(), "terrain", terrain))
				Edit(doc, entity, "terrain",
				    Archive(AuthorJson::Json{{"asset", terrain.ToString()}, {"viewDistance", 250.f},
				        {"lodDistance", 50.f}, {"enabled", true}, {"collisionEnabled", true}}),
				    error, &assets, terrain);
		}
	} catch (const std::exception& e) {
		error = e.what();
	}
} /* 命名空间 Catalpa::Editor */
