#include "Panels/EditorText.h"
#include "AssetEditors/PhysicsNavigationEditor.h"

#include "Commands/GraphCommands.h"
#include "Core/Application.h"
#include "Core/Jobs/JobSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Navigation/NavigationWorld.h"
#include "AssetPipeline/Navigation/RecastNavMeshBaker.h"
#include "AssetPipeline/Navigation/NavigationBuildProfiles.h"
#include "Core/IO/VirtualFileSystem.h"

#include <imgui.h>

namespace Catalpa::Editor {
	struct PhysicsNavigationEditor::Impl {
		struct Bake {
			NavMeshAsset asset;
			NavigationPath path;
			std::string error;
			bool success = false;
		};
		std::shared_ptr<Bake> bake;
		JobHandle job;
		JobSystem* jobs = nullptr;
		std::string source;
		Math::Vec3 start{}, end{3, 0, 3};
		bool success = false;
		bool centralRayHit = false;
		std::vector<Math::Vec3> curve;
		bool motionCurve = false;
		std::string profiles;
		std::filesystem::path profilePath;
		std::filesystem::file_time_type profileTime{};
	};

	PhysicsNavigationEditor::PhysicsNavigationEditor() : m_Impl(std::make_unique<Impl>()) {}

	PhysicsNavigationEditor::~PhysicsNavigationEditor() {
		if (m_Impl->jobs) {
			m_Impl->job.Cancel();
			std::string ignored;
			if (m_Impl->job.HasValue() && !m_Impl->jobs->IsComplete(m_Impl->job) &&
			    !m_Impl->jobs->Wait(m_Impl->job, ignored) && !m_Impl->jobs->IsComplete(m_Impl->job))
				std::terminate();
			m_Impl->jobs->CollectCompleted();
		}
	}

	bool PhysicsNavigationEditor::Accepts(const AssetDocument& doc) const {
		const auto name = doc.AuthorPath().filename().string();
		return doc.TypeKey() == "collision_shape" || doc.TypeKey() == "physics_material" ||
		    doc.TypeKey() == "navmesh" || name.ends_with(".collision.json") ||
		    name.ends_with(".physics-material.json") || name.ends_with(".navmesh.json");
	}

	void PhysicsNavigationEditor::Close(Application&) {
		m_Impl->job.Cancel();
		if (m_Impl->jobs && m_Impl->job.HasValue()) {
			std::string ignored;
			if (!m_Impl->jobs->Wait(m_Impl->job, ignored) && !m_Impl->jobs->IsComplete(m_Impl->job))
				std::terminate();
		}
		if (m_Impl->jobs)
			m_Impl->jobs->CollectCompleted();
		m_Impl->job = {};
		m_Impl->jobs = nullptr;
		m_Impl->bake.reset();
		m_Impl->source.clear();
		m_Impl->success = false;
		m_Impl->curve.clear();
	}

	void PhysicsNavigationEditor::Draw(
	    Application& app, AssetDocument& doc, const PanelText& text, std::string& error) {
		const auto T = [&](const char* key, const char* fallback) {
			return text ? text(key, fallback, key) : std::string(fallback);
		};
		if (!doc.Source()) {
			ImGui::TextUnformatted(UiText("physics.no_source", "No author source"));
			return;
		}
		const auto& source = *doc.Source();
		std::string next;
		if (doc.TypeKey() == "physics_material" ||
		    doc.AuthorPath().filename().string().ends_with(".physics-material.json")) {
			PhysicsMaterialAsset value;
			if (!ReadPhysicsMaterial(source, value, error))
				return;
			bool changed = ImGui::SliderFloat(T("physics.friction", "Friction").c_str(), &value.friction, 0, 2);
			changed |= ImGui::SliderFloat(T("physics.restitution", "Restitution").c_str(), &value.restitution, 0, 1);
			if (changed && WritePhysicsMaterial(value, next, error)) {
				ChangeAuthorSource(doc, std::move(next), error);
				m_Impl->curve.clear();
			}
			/* 用独立的物理世界试算当前材质的摩擦与回弹，避免改变编辑场景。 */
			if (ImGui::Button(T("physics.material_preview", "Preview friction / restitution (4 seconds)").c_str())) {
				PhysicsWorld world;
				CollisionShapeAsset floor, box;
				floor.dimensions = {10, 0.5f, 10};
				PhysicsBodyDesc ground;
				ground.pose.position = {0, -0.5f, 0};
				PhysicsBodyHandle groundBody, movingBody;
				PhysicsBodyDesc moving;
				moving.motion = PhysicsMotion::Dynamic;
				moving.pose.position = {-3, 3, 0};
				moving.linearVelocity = {3, 0, 0};
				bool valid = world.Initialize({}, error) && world.CreateBody(ground, floor, value, groundBody, error) &&
				    world.CreateBody(moving, box, value, movingBody, error);
				std::vector<Math::Vec3> samples;
				for (uint64_t tick = 1; tick <= 240 && valid; ++tick) {
					PhysicsBodyState state;
					valid = world.Step(1.0 / 60, tick, error) && world.ReadBody(movingBody, state, error);
					if (valid && tick % 4 == 0)
						samples.push_back(state.pose.position);
				}
				if (valid) {
					m_Impl->curve = std::move(samples);
					m_Impl->motionCurve = true;
				}
			}
			ImGui::TextUnformatted(UiText("physics.si_note",
			    "SI units; material changes create a new cooked version. Runtime rebuild keeps its "
			    "old body if a candidate fails."));
		} else if (doc.TypeKey() == "collision_shape" ||
		    doc.AuthorPath().filename().string().ends_with(".collision.json")) {
			CollisionShapeAsset value;
			if (!ReadCollisionShape(source, value, error))
				return;
			int kind = int(value.kind);
			bool changed = false;
			if (value.vertices.empty()) {
				changed = ImGui::Combo(T("physics.primitive", "Primitive").c_str(), &kind,
				    UiChoices({UiText("choice.box", "Box"), UiText("choice.sphere", "Sphere"),
				                  UiText("choice.capsule", "Capsule")})
				        .c_str());
				value.kind = CollisionShapeKind(kind);
				changed |= ImGui::DragFloat3(T("physics.dimensions", "Half extents / radius, half-height (m)").c_str(),
				    &value.dimensions.x, 0.01f, 0.001f, 10000);
			}
			ImGui::Text(UiFormat("physics.primitive_counts", "Vertices: %zu; triangles: %zu"), value.vertices.size(),
			    value.indices.size() / 3);
			if (changed && WriteCollisionShape(value, next, error)) {
				ChangeAuthorSource(doc, std::move(next), error);
				m_Impl->success = false;
				m_Impl->curve.clear();
			}
			/* 用当前形状构建预览刚体，沿世界 Y 轴发射向下的射线并显示命中结果。 */
			if (ImGui::Button(T("physics.validate_shape", "Validate SDK shape and ground query").c_str())) {
				m_Impl->success = false;
				m_Impl->curve.clear();
				PhysicsWorld world;
				PhysicsBodyHandle body;
				PhysicsQueryHit hit;
				if (world.Initialize({}, error) && world.CreateBody({}, value, {}, body, error) &&
				    world.Raycast({0, 100, 0}, {0, -200, 0}, hit, error)) {
					m_Impl->success = true;
					m_Impl->centralRayHit = hit.hit;
					Math::Vec3 low = -value.dimensions, high = value.dimensions;
					if (value.kind == CollisionShapeKind::Sphere) {
						low = Math::Vec3(-value.dimensions.x);
						high = -low;
					}
					if (value.kind == CollisionShapeKind::Capsule) {
						low = {-value.dimensions.x, -value.dimensions.x - value.dimensions.y, -value.dimensions.x};
						high = -low;
					}
					if (!value.vertices.empty()) {
						low = high = value.vertices.front();
						for (auto p : value.vertices) {
							low = glm::min(low, p);
							high = glm::max(high, p);
						}
					}
					std::vector<Math::Vec3> samples;
					const float span = std::max(high.y - low.y, 0.01f), z = (low.z + high.z) * 0.5f;
					for (int n = 0; n <= 48; ++n) {
						const float x = low.x + (high.x - low.x) * n / 48;
						PhysicsQueryHit result;
						if (!world.Raycast({x, high.y + span, z}, {0, -3 * span, 0}, result, error)) {
							m_Impl->success = false;
							break;
						}
						if (result.hit)
							samples.push_back(result.point);
					}
					if (m_Impl->success) {
						m_Impl->curve = std::move(samples);
						m_Impl->motionCurve = false;
					}
				}
			}
			if (m_Impl->success)
				ImGui::TextUnformatted(m_Impl->centralRayHit
				        ? UiText("physics.ray_hit",
				              "Jolt shape created; central ray hit. Preview uses the authored metre-space shape.")
				        : UiText("physics.ray_missed", "Jolt shape created; the central ray missed."));
		} else {
			const auto profilePath = app.GetResourceRoot() / "Config" / "NavigationBuildProfiles.json";
			std::error_code profileError;
			const auto profileTime = std::filesystem::last_write_time(profilePath, profileError);
			if (profileError) {
				error = "Cannot inspect navigation build profiles: " + profileError.message();
				return;
			}
			if (m_Impl->profiles.empty() || m_Impl->profilePath != profilePath || m_Impl->profileTime != profileTime) {
				VirtualFileSystem files;
				VirtualPath path;
				std::vector<std::byte> bytes;
				if (!files.MountReadOnly("navigation-config", app.GetResourceRoot() / "Config", 0, error) ||
				    !VirtualPath::Parse("navigation-config:/NavigationBuildProfiles.json", path, error) ||
				    !files.ReadFile(path, 65536, bytes, error))
					return;
				m_Impl->profiles.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				m_Impl->profilePath = profilePath;
				m_Impl->profileTime = profileTime;
			}
			NavigationBuildInput value;
			if (!Tools::ResolveNavigationBuildSource(source, m_Impl->profiles, value, error))
				return;
			ImGui::Text("%s: %s", UiText("navigation.profile", "Build profile"), value.buildProfile.c_str());
			bool changed = ImGui::DragFloat(
			    T("navigation.cell_size", "Cell size (m)").c_str(), &value.settings.cellSize, 0.01f, 0.01f, 10);
			changed |= ImGui::DragFloat(
			    T("navigation.cell_height", "Cell height (m)").c_str(), &value.settings.cellHeight, 0.01f, 0.01f, 10);
			changed |= ImGui::DragFloat(T("navigation.agent_height", "Agent height (m)").c_str(),
			    &value.settings.agentHeight, 0.01f, 0.03f, 100);
			changed |= ImGui::DragFloat(
			    T("navigation.agent_radius", "Agent radius (m)").c_str(), &value.settings.agentRadius, 0.01f, 0, 20);
			changed |= ImGui::DragFloat(
			    T("navigation.step_height", "Step height (m)").c_str(), &value.settings.maxClimb, 0.01f, 0, 10);
			changed |= ImGui::SliderFloat(
			    T("navigation.slope", "Slope limit (degrees)").c_str(), &value.settings.maxSlopeDegrees, 0, 89);
			if (changed && WriteNavigationSource(value, next, error))
				ChangeAuthorSource(doc, std::move(next), error);
			ImGui::InputFloat3(T("navigation.path_start", "Path start (m)").c_str(), &m_Impl->start.x);
			ImGui::InputFloat3(T("navigation.path_end", "Path end (m)").c_str(), &m_Impl->end.x);
			const bool running = m_Impl->job.HasValue() && m_Impl->jobs && !m_Impl->jobs->IsComplete(m_Impl->job);
			ImGui::BeginDisabled(running);
			/* 快照化烘焙参数和路径端点，在后台生成导航网格并查询两点之间的路径。 */
			if (ImGui::Button(T("navigation.bake", "Bake and preview path").c_str())) {
				m_Impl->bake = std::make_shared<Impl::Bake>();
				m_Impl->jobs = &app.GetJobs();
				m_Impl->source = source + m_Impl->profiles;
				auto result = m_Impl->bake;
				const auto start = m_Impl->start, end = m_Impl->end;
				if (!m_Impl->jobs->Submit(
				        [value, result, start, end](const CancellationToken& token) {
					        if (!Tools::RecastNavMeshBaker::Bake(value, token, result->asset, result->error))
						        return;
					        NavigationWorld world;
					        NavigationPathRequest request;
					        request.start = start;
					        request.end = end;
					        result->success =
					            world.Load(std::make_shared<const NavMeshAsset>(result->asset), 1, result->error) &&
					            world.FindPath(request, token, result->path, result->error);
				        },
				        m_Impl->job, error))
					m_Impl->bake.reset();
			}
			ImGui::EndDisabled();
			if (running) {
				ImGui::SameLine();
				if (ImGui::Button(T("navigation.cancel", "Cancel bake").c_str()))
					m_Impl->job.Cancel();
			}
			/* 源文档和区域配置仍匹配提交时的快照才展示任务结果，避免将旧路径绘制到新网格。 */
			if (m_Impl->job.HasValue() && m_Impl->jobs && m_Impl->jobs->IsComplete(m_Impl->job) && m_Impl->bake &&
			    m_Impl->source == source + m_Impl->profiles) {
				if (m_Impl->bake->error.empty() && !m_Impl->job.GetError().empty())
					m_Impl->bake->error = m_Impl->job.GetError();
				if (!m_Impl->bake->error.empty())
					ImGui::TextWrapped("%s", m_Impl->bake->error.c_str());
				if (m_Impl->bake->success) {
					ImGui::Text(UiFormat("nav.tile_bytes", "Tile bytes: %zu; path points: %zu"),
					    m_Impl->bake->asset.tile.size(), m_Impl->bake->path.points.size());
					const auto origin = ImGui::GetCursorScreenPos();
					const ImVec2 size{ImGui::GetContentRegionAvail().x, 240};
					ImGui::InvisibleButton("Navigation top-down preview", size);
					auto* draw = ImGui::GetWindowDrawList();
					draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(25, 28, 30, 255));
					auto low = value.vertices.front(), high = low;
					for (auto p : value.vertices) {
						low = glm::min(low, p);
						high = glm::max(high, p);
					}
					const float scale = std::min((size.x - 16) / std::max(high.x - low.x, 0.01f),
					    (size.y - 16) / std::max(high.z - low.z, 0.01f));
					const auto project = [&](Math::Vec3 p) {
						return ImVec2(origin.x + 8 + (p.x - low.x) * scale, origin.y + 8 + (p.z - low.z) * scale);
					};
					for (size_t n = 0; n < value.indices.size(); n += 3) {
						const auto area = value.areas.empty() ? 0 : value.areas[n / 3];
						const ImU32 color = IM_COL32(50 + area * 17 % 150, 100 + area * 29 % 100, 100, 255);
						for (int k = 0; k < 3; ++k)
							draw->AddLine(project(value.vertices[value.indices[n + k]]),
							    project(value.vertices[value.indices[n + (k + 1) % 3]]), color);
					}
					const auto& points = m_Impl->bake->path.points;
					for (size_t n = 1; n < points.size(); ++n)
						draw->AddLine(project(points[n - 1]), project(points[n]), IM_COL32(255, 210, 60, 255), 3);
					draw->AddCircleFilled(project(m_Impl->start), std::max(3.0f, value.settings.agentRadius * scale),
					    IM_COL32(60, 220, 120, 255));
				}
			}
			ImGui::TextUnformatted(UiText("nav.bake_view",
			    "Top-down source regions / agent radius / actual Detour path. Bake is cancellable "
			    "and leaves source geometry untouched."));
		}
		if (doc.TypeKey() != "navmesh" && !doc.AuthorPath().filename().string().ends_with(".navmesh.json") &&
		    !m_Impl->curve.empty()) {
			ImGui::TextUnformatted(m_Impl->motionCurve
			        ? UiText("physics.drop_curve", "Jolt drop / bounce / friction trajectory (X/Y metres)")
			        : UiText("physics.ray_curve", "Jolt ray-sampled shape cross section (X/Y metres)"));
			auto low = m_Impl->curve.front(), high = low;
			for (auto p : m_Impl->curve) {
				low = glm::min(low, p);
				high = glm::max(high, p);
			}
			const auto origin = ImGui::GetCursorScreenPos();
			const ImVec2 size{std::max(ImGui::GetContentRegionAvail().x, 32.0f), 180};
			ImGui::InvisibleButton("Physics value preview", size);
			auto* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(24, 26, 30, 255));
			const float scale = std::min(
			    (size.x - 16) / std::max(high.x - low.x, 0.01f), (size.y - 16) / std::max(high.y - low.y, 0.01f));
			const auto project = [&](Math::Vec3 p) {
				return ImVec2(origin.x + 8 + (p.x - low.x) * scale, origin.y + size.y - 8 - (p.y - low.y) * scale);
			};
			for (size_t i = 0; i < m_Impl->curve.size(); ++i) {
				const auto p = project(m_Impl->curve[i]);
				draw->AddCircleFilled(p, 2, IM_COL32(100, 220, 170, 255));
				if (i && m_Impl->motionCurve)
					draw->AddLine(project(m_Impl->curve[i - 1]), p, IM_COL32(100, 220, 170, 255));
			}
		}
	}
} /* 命名空间 Catalpa::Editor */
