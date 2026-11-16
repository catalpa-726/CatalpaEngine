#include "Application/EditorWorkspace.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "Project/ProjectCreation.h"
#include "Scene/SceneSerializer.h"
#include "TestRunner.h"
#include "imgui_internal.h"

#include <chrono>
#include <fstream>

namespace Catalpa::Tests {
	namespace {
		/** @brief 将项目内 OBJ 通过实际 ImGui 指针拖到场景图像，复核选中实体和磁盘作者场景。 */
		class WorkspaceModelDropTest final : public Layer {
		public:
			WorkspaceModelDropTest() : Layer("WorkspaceModelDropTest") {}

			void OnAttach() override {
				std::filesystem::path project;
				std::string error;
				if (!m_Fixture.Create(error) || !Tools::CreateProject(m_Fixture.root, "DropQA", project, error)) {
					Finish(false, error);
					return;
				}
				const auto content = project.parent_path() / "Content";
				m_Obj = content / "00-Drop.obj";
				m_SceneFile = content / "Scenes/Main.scene";
				if (!AtomicFileWriter::Write(m_Obj,
				        "mtllib z-Drop.mtl\no Probe\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
				        "vn 0 0 1\nusemtl ProbeMaterial\nf 1/1/1 2/2/1 3/3/1\n",
				        error) ||
				    !AtomicFileWriter::Write(content / "z-Drop.mtl",
				        "newmtl ProbeMaterial\nKd 0.5 0.6 0.7\nmap_Bump absent-normal.tga\n", error)) {
					Finish(false, error);
					return;
				}
				m_SourcePath = Tools::AssetPathText(std::filesystem::canonical(m_Obj));
				const auto settings = m_Fixture.root / "Workspace.json";
				m_Fixture.files = {settings, m_Fixture.root / "Workspace.json.bak"};
				m_Workspace = std::make_unique<Editor::EditorWorkspace>(project, settings);
				m_Workspace->OnAttach();
				ImGuiContextHook hook{};
				hook.Type = ImGuiContextHookType_NewFramePre;
				hook.UserData = this;
				hook.Callback = [](ImGuiContext*, ImGuiContextHook* h) {
					static_cast<WorkspaceModelDropTest*>(h->UserData)->DispatchInput();
				};
				m_Hook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
				m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
			}

			void OnDetach() override {
				if (m_Hook)
					ImGui::RemoveContextHook(ImGui::GetCurrentContext(), m_Hook);
				if (m_Workspace) {
					m_Workspace->OnDetach();
					m_Workspace.reset();
				}
				if (!m_Fixture.root.empty()) {
					std::error_code ignored;
					std::filesystem::remove_all(m_Fixture.root / "DropQA", ignored);
				}
			}

			void OnEvent(Event& event) override {
				if (m_Finished || !m_Workspace)
					return;
				if (event.GetEventType() == EventType::WindowClose)
					Finish(false, "Workspace closed before the model was saved");
				else
					m_Workspace->OnEvent(event);
			}

			void OnUpdate(Deltatime dt) override {
				if (m_Finished || !m_Workspace)
					return;
				m_Workspace->OnUpdate(dt);
				if (std::chrono::steady_clock::now() > m_Deadline)
					Finish(false, "Timed out waiting for model drag, selection or save");
			}

			void OnImGuiRender() override {
				if (m_Finished || !m_Workspace)
					return;
				const bool logInspector = m_Stage == Stage::Placement;
				if (logInspector)
					ImGui::LogToBuffer(0);
				m_Workspace->OnImGuiRender();
				++m_Frame;
				if (logInspector) {
					const auto& buffer = ImGui::GetCurrentContext()->LogBuffer;
					const std::string drawn(buffer.c_str(), static_cast<size_t>(buffer.size()));
					ImGui::LogFinish();
					CheckPlacement(drawn);
				} else if (m_Stage == Stage::Save) {
					if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
						m_SaveKeys = false;
						CheckSaved();
					}
				} else if (m_Frame >= 6) {
					DriveDrag();
				}
			}

		private:
			enum class Stage { FindSource, Press, StartDrag, Move, Preview, Placement, Save };

			/** @brief 仅向当前测试的 ImGui 上下文投递输入，保留后端正常的逐帧处理。 */
			void DispatchInput() const {
				if (!m_PositionSet)
					return;
				auto& io = ImGui::GetIO();
				io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
				io.AddMousePosEvent(m_Position.x, m_Position.y);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, m_Down);
				io.AddKeyEvent(ImGuiMod_Ctrl, m_SaveKeys);
				io.AddKeyEvent(ImGuiKey_S, m_SaveKeys);
			}

			void DriveDrag() {
				auto& g = *ImGui::GetCurrentContext();
				auto* scene = ImGui::FindWindowByName("###Catalpa.Viewport");
				auto* project = ImGui::FindWindowByName("###Catalpa.Content");
				if (!project || !scene)
					return;
				if (m_Stage == Stage::FindSource) {
					if (project->SkipItems || scene->SkipItems) {
						ImGui::FocusWindow(project->SkipItems ? project : scene);
						return;
					}
					auto* live = Application::Get().GetScenes().GetActive();
					if (!live || !m_InitialCount) {
						SceneAsset initial;
						std::string error;
						if (live && SceneSerializer::Capture(*live, initial, error) &&
						    std::any_of(initial.entities.begin(), initial.entities.end(),
						        [](const auto& node) { return node.name == "Main Camera"; }))
							m_InitialCount = initial.entities.size();
						return;
					}
					for (auto* child : project->DC.ChildWindows) {
						if (child->ChildId != project->GetID("Files"))
							continue;
						m_Item = ImHashStr("##File", 0, child->GetID(m_SourcePath.c_str()));
						if (m_PositionSet && g.HoveredId == m_Item) {
							m_Source = g.IO.MousePos;
							m_Target = scene->InnerRect.GetCenter();
							m_Down = true;
							m_Stage = Stage::Press;
							return;
						}
						const auto clip = child->InnerClipRect;
						if (clip.GetHeight() < 10 || clip.GetWidth() < 12) {
							ImGui::SetScrollY(project, project->ScrollMax.y);
							return;
						}
						/* 按可见卡片区域抽样，只有命中真实 OBJ 控件 ID 才会按下。 */
						const int columns = std::max(1, static_cast<int>(clip.GetWidth() / 22));
						const int rows = std::max(1, static_cast<int>(clip.GetHeight() / 16));
						const int cell = m_Sample++ % (rows * columns);
						m_Position = {clip.Min.x + (cell % columns + .5f) * clip.GetWidth() / columns,
						    clip.Min.y + (cell / columns + .5f) * clip.GetHeight() / rows};
						m_PositionSet = true;
						return;
					}
					return;
				}
				if (m_Stage == Stage::Press && g.IO.MouseDown[0] && g.ActiveId == m_Item) {
					m_Position = {m_Source.x + 2 * g.IO.MouseDragThreshold + 2, m_Source.y};
					m_Stage = Stage::StartDrag;
				} else if (m_Stage == Stage::StartDrag) {
					const auto* payload = ImGui::GetDragDropPayload();
					if (!payload)
						return;
					if (!payload->IsDataType("CATALPA_SOURCE") || payload->SourceId != m_Item ||
					    payload->DataSize != static_cast<int>(m_SourcePath.size() + 1) ||
					    std::memcmp(payload->Data, m_SourcePath.c_str(), m_SourcePath.size() + 1) != 0) {
						Finish(false, "Unexpected source ID, payload type or OBJ path");
						return;
					}
					CAT_INFO("[Editor OBJ drop] Project file emitted CATALPA_SOURCE for {}", m_SourcePath);
					m_Stage = Stage::Move;
				} else if (m_Stage == Stage::Move) {
					const float fraction = ++m_MoveFrames / 5.f;
					m_Position = {m_Source.x + (m_Target.x - m_Source.x) * fraction,
					    m_Source.y + (m_Target.y - m_Source.y) * fraction};
					if (m_MoveFrames == 5)
						m_Stage = Stage::Preview;
				} else if (m_Stage == Stage::Preview) {
					const auto* payload = ImGui::GetDragDropPayload();
					if (g.HoveredWindow == scene && payload && payload->IsPreview() && g.DragDropAcceptIdCurr &&
					    g.DragDropAcceptIdCurr == g.DragDropAcceptIdPrev) {
						m_Down = false;
						m_Stage = Stage::Placement;
						CAT_INFO("[Editor OBJ drop] Scene image accepted the preview; releasing the mouse");
					}
				}
			}

			/** @brief 核对新增模型锚点、属于它的网格及材质，避免其他场景实体误报通过。 */
			static PersistentEntityId ModelRoot(const SceneAsset& scene) {
				const auto root = std::find_if(scene.entities.begin(), scene.entities.end(),
				    [](const auto& node) { return node.name == "00-Drop" && !node.parent.HasValue(); });
				if (root == scene.entities.end())
					return {};
				for (const auto& node : scene.entities) {
					if (!node.mesh || !node.mesh->mesh.HasValue() || node.mesh->materials.empty() ||
					    !node.mesh->materials.front().HasValue())
						continue;
					auto parent = node.parent;
					for (size_t depth = 0; parent.HasValue() && depth < scene.entities.size(); ++depth) {
						if (parent == root->id)
							return root->id;
						const auto ancestor = std::find_if(scene.entities.begin(), scene.entities.end(),
						    [parent](const auto& candidate) { return candidate.id == parent; });
						if (ancestor == scene.entities.end())
							break;
						parent = ancestor->parent;
					}
				}
				return {};
			}

			void CheckPlacement(const std::string& drawn) {
				const auto* live = Application::Get().GetScenes().GetActive();
				SceneAsset placed;
				std::string error;
				if (!live || !SceneSerializer::Capture(*live, placed, error) ||
				    placed.entities.size() <= m_InitialCount)
					return;
				m_Placed = ModelRoot(placed);
				if (!m_Placed.HasValue()) {
					Finish(false, "New scene subtree has no model root, mesh or material");
					return;
				}
				/* Inspector 的实体名称输入框之后会绘制当前选中实体的持久身份。 */
				auto* inspector = ImGui::FindWindowByName("###Catalpa.Inspector");
				if (inspector && inspector->SkipItems) {
					ImGui::FocusWindow(inspector);
					return;
				}
				if (!inspector || drawn.find(m_Placed.ToString()) == drawn.npos)
					return;
				m_PlacedCount = placed.entities.size();
				m_SaveKeys = true;
				m_Stage = Stage::Save;
				CAT_INFO("[Editor OBJ drop] Inspector selected model {}, entities {} -> {}", m_Placed.ToString(),
				    m_InitialCount, m_PlacedCount);
			}

			void CheckSaved() {
				std::ifstream input(m_SceneFile, std::ios::binary);
				const std::string bytes{std::istreambuf_iterator<char>(input), {}};
				SceneAsset saved;
				std::string error;
				if (!SceneSerializer::Read(bytes, saved, error) || saved.entities.size() != m_PlacedCount ||
				    ModelRoot(saved) != m_Placed)
					Finish(false, "Ctrl+S did not persist the placed mesh and material: " + error);
				else
					Finish(true, "Dragged OBJ into Scene, selected the placed root and saved its mesh and material");
			}

			void Finish(bool passed, const std::string& reason) {
				m_Finished = true;
				if (passed)
					CAT_INFO("[Editor OBJ drop PASS] {}", reason);
				else
					CAT_ERROR("[Editor OBJ drop FAIL] {} (stage {}, frame {}, samples {})", reason,
					    static_cast<int>(m_Stage), m_Frame, m_Sample);
				Application::Get().RequestClose(passed ? 0 : 1);
			}

			AssetTest::Fixture m_Fixture;
			std::unique_ptr<Editor::EditorWorkspace> m_Workspace;
			std::filesystem::path m_Obj, m_SceneFile;
			std::string m_SourcePath;
			std::chrono::steady_clock::time_point m_Deadline{};
			PersistentEntityId m_Placed{};
			ImGuiID m_Hook{}, m_Item{};
			ImVec2 m_Position{}, m_Source{}, m_Target{};
			size_t m_InitialCount{}, m_PlacedCount{};
			Stage m_Stage = Stage::FindSource;
			int m_Frame{}, m_Sample{}, m_MoveFrames{};
			bool m_PositionSet{}, m_Down{}, m_SaveKeys{}, m_Finished{};
		};
	}

	std::unique_ptr<Layer> CreateWorkspaceModelDropTest() {
		return std::make_unique<WorkspaceModelDropTest>();
	}
}
