#include "Application/EditorWorkspace.h"
#include "Application/WorkspaceEditing.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "Project/ProjectCreation.h"
#include "Platform/ExternalScriptEditor.h"
#include "Documents/DocumentRecovery.h"
#include "TestRunner.h"
#include "imgui_internal.h"

#include <chrono>

namespace Catalpa::Tests {
	namespace {
		/** @brief 驱动真实工作区菜单，在两种图编辑器之间切换并观察节点连续绘制。 */
		class WorkspaceGraphTest final : public Layer {
		public:
			explicit WorkspaceGraphTest(bool project) : Layer("WorkspaceGraphTest"), m_WithProject(project) {}

			void OnAttach() override {
				std::string error;
				std::filesystem::path project;
				if (!m_Fixture.Create(error) ||
				    (m_WithProject && !Tools::CreateProject(m_Fixture.root, "Graphs", project, error))) {
					Finish(false, error);
					return;
				}
				const auto settings = m_Fixture.root / "Workspace.json";
				m_Fixture.files = {settings, m_Fixture.root / "Workspace.json.bak"};
				if (!m_WithProject && !VerifyStandaloneSave()) {
					Finish(false, "Could not create or protect a standalone C# behaviour");
					return;
				}
				m_Workspace = std::make_unique<Editor::EditorWorkspace>(project, settings);
				m_Workspace->OnAttach();
				m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
			}

			void OnDetach() override {
				if (m_Workspace) {
					m_Workspace->OnDetach();
					m_Workspace.reset();
				}
				/* 文件夹由本用例独占创建，含自动恢复等按文档身份命名的派生文件。 */
				if (m_WithProject && !m_Fixture.root.empty()) {
					std::error_code ignored;
					std::filesystem::remove_all(m_Fixture.root / "Graphs", ignored);
				}
			}

			void OnEvent(Event& event) override {
				if (m_Finished || !m_Workspace)
					return;
				if (event.GetEventType() == EventType::WindowClose)
					Finish(false, "Workspace closed before graph verification");
				else
					m_Workspace->OnEvent(event);
			}

			void OnUpdate(Deltatime dt) override {
				if (!m_Finished && m_Workspace) {
					m_Workspace->OnUpdate(dt);
					if (std::chrono::steady_clock::now() > m_Deadline)
						Finish(false, "Timed out waiting for the graph menu, canvas or draft selector");
				}
			}

			void OnImGuiRender() override {
				if (m_Finished || !m_Workspace)
					return;
				ImGui::LogToBuffer(0);
				m_Workspace->OnImGuiRender();
				const auto& buffer = ImGui::GetCurrentContext()->LogBuffer;
				const std::string drawn(buffer.c_str(), static_cast<size_t>(buffer.size()));
				ImGui::LogFinish();
				++m_Frame;
				if (m_Frame < 6)
					return;
				if (!m_WithProject && m_SessionIndex < 2) {
					static constexpr const char* commands[]{"###ui.new_scene", "###ui.new_material"};
					static constexpr const char* markers[]{"Untitled scene", "Surface properties"};
					if (m_SessionPending) {
						const bool ready = drawn.find(markers[m_SessionIndex]) != drawn.npos;
						if (ready) {
							++m_SessionIndex;
							m_SessionPending = false;
						}
					} else
						m_SessionPending = ActivateDocumentCommand(commands[m_SessionIndex], drawn);
					return;
				}
				if (m_Stage == Stage::NewBehavior || m_Stage == Stage::NewMaterial) {
					const bool behavior = m_Stage == Stage::NewBehavior;
					if (ActivateDocumentCommand(
					        behavior ? "###ui.new_behavior_graph" : "###ui.new_material_graph", drawn))
						m_Stage = behavior ? Stage::Behavior : Stage::Material;
					return;
				}
				auto* editor = ImGui::FindWindowByName("###Catalpa.AssetEditor");
				if (!editor || !editor->Active)
					return;
				if (editor->SkipItems) {
					ImGui::FocusWindow(editor);
					return;
				}
				if (m_Stage == Stage::ChooseBehavior) {
					auto* choices = ImGui::FindWindowByName("##Combo_00");
					const auto& label = m_BehaviorTitle;
					if (choices && choices->Active && !choices->SkipItems && drawn.find(label) != drawn.npos) {
						ImGui::ActivateItemByID(choices->GetID("###behavior_graph1"));
						m_Stage = Stage::Switching;
					} else if (m_Frame % 8 == 0)
						ImGui::ActivateItemByID(editor->GetID("###OpenDrafts"));
					return;
				}
				/* 端口的方向前缀只在 GraphCanvas 的节点循环内提交。 */
				const bool material = m_Stage == Stage::Material;
				const bool node = drawn.find(material ? "Surface Output" : "Update") != drawn.npos;
				const bool pin = drawn.find(material ? "> Base Color" : "< Next") != drawn.npos;
				const auto draft = DrawnDraft(drawn);
				if (!node || !pin || draft.empty() || editor->DrawList->VtxBuffer.empty()) {
					m_Consecutive = 0;
					return;
				}
				/* 选中新行的当帧，组合框已绘制先前身份，而后面的画布会立即切换。 */
				if (m_Stage == Stage::Switching) {
					if (draft != m_BehaviorTitle)
						return;
					m_Stage = Stage::Restored;
				}
				if (m_Stage == Stage::Behavior)
					m_BehaviorTitle = draft;
				else if ((material && draft == m_BehaviorTitle) ||
				    (m_Stage == Stage::Restored && draft != m_BehaviorTitle)) {
					Finish(false, "Node canvas rendered for the wrong draft");
					return;
				}
				if (draft.find(material ? "Material graph" : "Behavior graph") == draft.npos) {
					Finish(false, "Draft selector did not show the active graph name");
					return;
				}
				if (++m_Consecutive < 4)
					return;
				m_Consecutive = 0;
				CAT_INFO("[Editor graphs] {} draft {} drew its initial node and pin for four consecutive frames",
				    material ? "Material" : "Behavior", draft);
				if (m_Stage == Stage::Restored)
					Finish(true, "Created both graph types from Documents and restored the original behavior canvas");
				else
					m_Stage = material ? Stage::ChooseBehavior : Stage::NewMaterial;
			}

		private:
			enum class Stage { NewBehavior, Behavior, NewMaterial, Material, ChooseBehavior, Switching, Restored };

			/** @brief 核对新 C# 文件可直接交给 IDE，重复创建也不会覆盖用户代码。 */
			bool VerifyStandaloneSave() {
				const auto path = m_Fixture.root / std::filesystem::u8path("玩家 Move.cs");
				m_Fixture.files.push_back(path);
				std::string error, original, reread;
				return Editor::CreateBehaviourFile(path, m_Fixture.root, error) &&
				    Editor::DocumentRecovery::Read(path, original, error) &&
				    original.find("class @玩家_Move : global::Catalpa.Behaviour") != original.npos &&
				    !Editor::CreateBehaviourFile(path, m_Fixture.root, error) &&
				    Editor::DocumentRecovery::Read(path, reread, error) && reread == original;
			}

			/** @brief 从当前实际打开的文档菜单触发动作，菜单关闭时先激活菜单栏。 */
			bool ActivateDocumentCommand(const char* id, const std::string& drawn) {
				auto* menu = ImGui::FindWindowByName("###Menu_00");
				if (menu && menu->Active && !menu->SkipItems && drawn.find("New behavior graph") != drawn.npos) {
					ImGui::ActivateItemByID(menu->GetID(id));
					return true;
				}
				if (auto* bar = ImGui::FindWindowByName("##MainMenuBar"); bar && m_Frame % 8 == 0)
					ImGui::ActivateItemByID(ImHashStr("###ui.documents", 0, bar->GetID("##MenuBar")));
				return false;
			}

			/** @brief 读取标签后方组合框实际显示的草稿名，复核切换后的类型与未保存标记。 */
			static std::string DrawnDraft(const std::string& drawn) {
				const auto label = drawn.find("Open drafts");
				if (label == drawn.npos)
					return {};
				const auto begin = drawn.find('{', label);
				const auto end = begin == drawn.npos ? drawn.npos : drawn.find('}', begin + 1);
				if (end == drawn.npos)
					return {};
				const auto first = drawn.find_first_not_of(" \t\r\n", begin + 1);
				const auto last = drawn.find_last_not_of(" \t\r\n", end - 1);
				return first <= last && last < end ? drawn.substr(first, last - first + 1) : std::string{};
			}

			void Finish(bool passed, const std::string& reason) {
				m_Finished = true;
				if (passed)
					CAT_INFO("[Editor graphs PASS] {}", reason);
				else
					CAT_ERROR("[Editor graphs FAIL] {} (stage {}, frame {}, consecutive {})", reason, int(m_Stage),
					    m_Frame, m_Consecutive);
				Application::Get().RequestClose(passed ? 0 : 1);
			}

			AssetTest::Fixture m_Fixture;
			bool m_WithProject;
			int m_SessionIndex = 0;
			bool m_SessionPending = false;
			std::unique_ptr<Editor::EditorWorkspace> m_Workspace;
			std::chrono::steady_clock::time_point m_Deadline{};
			std::string m_BehaviorTitle;
			Stage m_Stage = Stage::NewBehavior;
			int m_Frame = 0, m_Consecutive = 0;
			bool m_Finished = false;
		};
	}

	std::unique_ptr<Layer> CreateWorkspaceGraphTest(bool project) {
		return std::make_unique<WorkspaceGraphTest>(project);
	}
}
