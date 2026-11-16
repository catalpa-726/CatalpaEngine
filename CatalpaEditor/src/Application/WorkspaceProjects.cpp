#include "catpch.h"
#include "Application/EditorWorkspace.h"
#include "Application/EditorProject.h"
#include "Platform/AuthorFileDialogs.h"
#include "Project/ProjectCreation.h"
#include "Panels/FormLayout.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Catalpa::Editor {
	namespace {
		std::string ProjectPathText(const std::filesystem::path& path) {
			const auto text = path.u8string();
			return {reinterpret_cast<const char*>(text.data()), text.size()};
		}
	}

	void EditorWorkspace::QueueProject(const std::filesystem::path& path) {
		const auto text = ProjectPathText(path);
		if (text.size() >= m_ProjectPathInput.size()) {
			m_ProjectDialogError = "Project path exceeds 2047 UTF-8 bytes";
			Report("status.project_failed", "Project open failed", m_ProjectDialogError);
			return;
		}
		std::copy(text.begin(), text.end(), m_ProjectPathInput.begin());
		m_ProjectPathInput[text.size()] = 0;
		m_ProjectDialogError.clear();
		m_Action = Action::OpenProject;
	}

	void EditorWorkspace::RememberProject() {
		if (m_ProjectFile.empty())
			return;
		auto& recent = m_Settings.recentProjects;
		const auto path = ProjectPathText(m_ProjectFile);
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		recent.insert(recent.begin(), path);
		if (recent.size() > 12)
			recent.resize(12);
		m_Dirty = true;
	}

	void EditorWorkspace::ChooseProject(bool folder) {
		std::filesystem::path path;
		bool cancelled = false;
		m_ProjectDialogError.clear();
		const auto initial = m_ProjectFile.empty() ? m_ExecutableDirectory : m_ProjectFile.parent_path();
		const bool selected = folder
		    ? SelectAuthorFolder(initial, path, cancelled, m_ProjectDialogError)
		    : SelectAuthorFile(false, initial, path, cancelled, m_ProjectDialogError, "catalpaproj");
		if (!selected) {
			Report("status.project_failed", "Project open failed", m_ProjectDialogError);
			return;
		}
		if (cancelled)
			return;
		if (!folder) {
			QueueProject(path);
			return;
		}
		if (!Tools::FindProjectFiles(path, m_ProjectCandidates, m_ProjectDialogError)) {
			Report("status.project_failed", "Project open failed", m_ProjectDialogError);
			return;
		}
		if (m_ProjectCandidates.size() == 1) {
			QueueProject(m_ProjectCandidates.front());
			m_ProjectCandidates.clear();
		} else if (m_ProjectCandidates.empty()) {
			m_ProjectDialogError = Text("project.no_description",
			    "No .catalpaproj was found in this directory. Choose its project root, or create a new project.");
			Report("status.project_failed", "Project open failed", m_ProjectDialogError);
		} else
			m_OpenProjectPopup = true;
	}

	/**
	 * @brief 绘制新建、导入、最近项目和无项目起始页。
	 * 原生选择器只取路径；实际项目切换在下一帧安全点完成，并复用未保存文档确认。
	 */
	void EditorWorkspace::DrawProjectLauncher() {
		const auto beginNew = [&] {
			m_ProjectDialogError.clear();
			m_NewProjectPopup = true;
		};
		const auto actions = [&] {
			if (ImGui::Button(Label("project.new", "New project...", "Launcher.New").c_str(), {-FLT_MIN, 0}))
				beginNew();
			if (ImGui::Button(Label("action.open_project", "Open Project...", "Launcher.Open").c_str(), {-FLT_MIN, 0}))
				ChooseProject(false);
			if (ImGui::Button(
			        Label("project.import", "Import project directory...", "Launcher.Import").c_str(), {-FLT_MIN, 0}))
				ChooseProject(true);
		};
		if (!m_Project) {
			const auto* center = ImGui::DockBuilderGetCentralNode(ImHashStr("Catalpa.Workspace.Dockspace"));
			if (center)
				ImGui::SetNextWindowDockID(center->ID, ImGuiCond_Once);
			if (ImGui::Begin(Label("project.welcome", "Welcome to Catalpa", "Catalpa.Welcome").c_str())) {
				ImGui::TextWrapped("%s",
				    Text("project.welcome_hint",
				        "Create or open an author project to edit scenes, import assets, add behavior and export a game."));
				actions();
				ImGui::SeparatorText(Text("project.recent", "Recent projects"));
				for (const auto& recent : m_Settings.recentProjects) {
					ImGui::PushID(recent.c_str());
					const auto path = std::filesystem::u8path(recent);
					std::error_code error;
					const bool exists = std::filesystem::is_regular_file(path, error);
					ImGui::BeginDisabled(!exists);
					if (ImGui::Selectable(ProjectPathText(path.stem()).c_str()))
						QueueProject(path);
					ImGui::EndDisabled();
					if (!exists && ImGui::SmallButton(Text("project.relocate", "Locate project...")))
						ChooseProject(false);
					ImGui::TextWrapped("%s", recent.c_str());
					ImGui::PopID();
				}
				if (!m_ProjectDialogError.empty())
					ImGui::TextWrapped("%s", m_ProjectDialogError.c_str());
			}
			ImGui::End();
		}
		const auto newTitle = Label("project.new", "New project...", "Catalpa.NewProject");
		if (m_NewProjectPopup) {
			ImGui::OpenPopup(newTitle.c_str(), ImGuiPopupFlags_NoOpenOverExistingPopup);
			if (ImGui::IsPopupOpen(newTitle.c_str()))
				m_NewProjectPopup = false;
		}
		const auto* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize({std::min(680.f, viewport->WorkSize.x * .9f), 0}, ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints({0, 0}, {viewport->WorkSize.x * .9f, viewport->WorkSize.y * .9f});
		PrepareEditorModal();
		if (ImGui::BeginPopupModal(newTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("%s",
			    Text("project.new_hint",
			        "Choose a parent directory. A new project subdirectory will contain Content, a Main scene, build settings and its project description."));
			ImGui::TextUnformatted(Text("project.name", "Project name"));
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##ProjectName", m_NewProjectName.data(), m_NewProjectName.size());
			ImGui::TextWrapped("%s", ProjectPathText(m_NewProjectParent).c_str());
			if (ImGui::Button(Label("project.choose_parent", "Choose parent directory...", "ProjectParent").c_str())) {
				bool cancelled = false;
				std::filesystem::path selected;
				if (SelectAuthorFolder(m_NewProjectParent, selected, cancelled, m_ProjectDialogError) && !cancelled)
					m_NewProjectParent = std::move(selected);
			}
			ImGui::BeginDisabled(m_NewProjectParent.empty() || !m_NewProjectName.front());
			if (ImGui::Button(Label("project.create", "Create project", "ConfirmCreateProject").c_str())) {
				std::filesystem::path created;
				if (Tools::CreateProject(m_NewProjectParent, m_NewProjectName.data(), created, m_ProjectDialogError)) {
					QueueProject(created);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(Label("project.cancel", "Cancel", "CancelCreateProject").c_str()))
				ImGui::CloseCurrentPopup();
			if (!m_ProjectDialogError.empty())
				ImGui::TextWrapped("%s", m_ProjectDialogError.c_str());
			ImGui::EndPopup();
		}
		const auto chooseTitle =
		    Label("project.choose_description", "Choose project description", "Catalpa.ChooseProject");
		if (m_OpenProjectPopup) {
			ImGui::OpenPopup(chooseTitle.c_str(), ImGuiPopupFlags_NoOpenOverExistingPopup);
			if (ImGui::IsPopupOpen(chooseTitle.c_str()))
				m_OpenProjectPopup = false;
		}
		ImGui::SetNextWindowSize({std::min(760.f, viewport->WorkSize.x * .9f), 0}, ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints({0, 0}, {viewport->WorkSize.x * .9f, viewport->WorkSize.y * .9f});
		PrepareEditorModal();
		if (ImGui::BeginPopupModal(chooseTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("%s",
			    Text("project.multiple",
			        "This directory contains multiple project descriptions. Choose the project to import."));
			ImGui::BeginChild(
			    "##ProjectChoices", {0, std::min(400.f, viewport->WorkSize.y * .5f)}, ImGuiChildFlags_Borders);
			for (const auto& candidate : m_ProjectCandidates) {
				if (ImGui::Selectable(ProjectPathText(candidate.filename()).c_str())) {
					QueueProject(candidate);
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", ProjectPathText(candidate).c_str());
			}
			ImGui::EndChild();
			if (ImGui::Button(Label("project.cancel", "Cancel", "CancelChooseProject").c_str()))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		// 工作区自动保存会更新状态栏，因此项目错误独立保留到用户关闭，避免原因一闪而过。
		if (m_Project && !m_ProjectDialogError.empty() && !ImGui::IsPopupOpen(newTitle.c_str())) {
			ImGui::SetNextWindowSize({std::min(680.f, viewport->WorkSize.x * .9f), 0}, ImGuiCond_Appearing);
			ImGui::SetNextWindowSizeConstraints({0, 0}, {viewport->WorkSize.x * .9f, viewport->WorkSize.y * .9f});
			bool open = true;
			if (ImGui::Begin(Label("status.project_failed", "Project open failed", "Catalpa.ProjectError").c_str(),
			        &open, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextWrapped("%s", m_ProjectDialogError.c_str());
				if (ImGui::Button(Label("project.cancel", "Cancel", "DismissProjectError").c_str()))
					open = false;
			}
			ImGui::End();
			if (!open)
				m_ProjectDialogError.clear();
		}
	}
}
