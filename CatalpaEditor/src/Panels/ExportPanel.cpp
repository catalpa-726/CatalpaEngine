#include "catpch.h"

#include "Panels/ExportPanel.h"
#include "Delivery/GameExport.h"
#include "AssetPipeline/AssetBuildTypes.h"
#include "Panels/EditorText.h"
#include "Panels/FormLayout.h"
#include "Platform/AuthorFileDialogs.h"
#include "imgui.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Catalpa::Editor {
	namespace {
		/** @brief 直接从交付目录启动程序，不经命令解释器拼接用户路径。 */
		bool LaunchGame(const std::filesystem::path& directory, std::string& error) {
			auto executable = directory / "CatalpaBox.exe";
			auto command =
			    L"\"" + executable.wstring() + L"\" --run-package \"" + (directory / "Game.catpkg").wstring() + L"\"";
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
			        directory.c_str(), &startup, &process)) {
				error = "CreateProcessW: " + std::to_string(GetLastError());
				return false;
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return true;
		}
	}

	ExportPanel::~ExportPanel() {
		m_Cancel.Cancel();
		if (m_Job.valid())
			m_Job.wait();
	}

	void ExportPanel::Draw(const Tools::ProjectSession* session, const std::filesystem::path& executableDirectory,
	    const std::filesystem::path& sceneSource, bool dirty, const std::function<bool()>& saveScene) {
		if (m_Job.valid() && m_Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			auto error = m_Job.get();
			if (!error.empty())
				m_Status = std::move(error);
			else {
				m_LastOutput = m_RunningOutput;
				m_Status = UiText("export.complete", "Export complete");
				if (m_Run) {
					if (!LaunchGame(m_LastOutput, m_Status))
						m_Visible = true;
					else
						m_Status = UiText("export.started", "Game started");
				}
			}
		}
		if (!m_Visible)
			return;
		const auto title = std::string(UiText("export.title", "Export Windows game")) + "###Catalpa.Export";
		const auto viewport = ImGui::GetMainViewport()->WorkSize;
		ImGui::SetNextWindowSize(
		    {std::min(620.f, viewport.x * .9f), std::min(340.f, viewport.y * .9f)}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(title.c_str(), &m_Visible)) {
			ImGui::End();
			return;
		}
		const bool busy = m_Job.valid();
		if (session && m_Project != session->config.projectFile && !busy) {
			m_Project = session->config.projectFile;
			m_Entry = sceneSource;
			m_Parent = m_Project.parent_path().parent_path();
		}
		if (m_UseSaved) {
			m_Entry = sceneSource;
			m_UseSaved = false;
		}
		if (!session)
			ImGui::TextUnformatted(UiText("export.no_project", "Open a project first"));
		ImGui::BeginDisabled(busy || !session);
		if (ImGui::Button(UiText("export.scene", "Select entry scene...")) && session) {
			std::filesystem::path entry;
			bool cancelled = false;
			if (SelectAuthorFile(false, session->config.contentRoot, entry, cancelled, m_Status, "scene,json") &&
			    !cancelled)
				m_Entry = std::move(entry);
		}
		ImGui::SameLine();
		ImGui::TextWrapped("%s", Tools::AssetPathText(m_Entry).c_str());
		if (saveScene && (m_Entry.empty() || (dirty && sceneSource == m_Entry)) &&
		    ImGui::Button(UiText("export.save", "Save active scene"))) {
			m_UseSaved = saveScene();
		}
		if (ImGui::Button(UiText("export.parent", "Choose output parent..."))) {
			std::filesystem::path parent;
			bool cancelled = false;
			if (SelectAuthorFolder(m_Parent, parent, cancelled, m_Status) && !cancelled)
				m_Parent = std::move(parent);
		}
		ImGui::SameLine();
		ImGui::TextWrapped("%s", Tools::AssetPathText(m_Parent).c_str());
		PlaceFormLabel(UiText("export.name", "New output folder"));
		ImGui::InputText("##ExportOutputName", m_Folder.data(), m_Folder.size());
		ImGui::BeginDisabled(m_Entry.empty() || (dirty && m_Entry == sceneSource));
		const bool exportOnly = ImGui::Button(UiText("export.build", "Export"));
		ImGui::SameLine();
		const bool run = ImGui::Button(UiText("export.run", "Export and run"));
		if ((run || exportOnly) && session) {
			const auto folder = std::filesystem::u8path(m_Folder.data());
			if (folder.empty() || folder.has_parent_path() || folder.has_root_path() || folder == "." || folder == "..")
				m_Status = UiText("export.invalid_name", "Enter a single folder name");
			else {
				m_RunningOutput = m_Parent / folder;
				m_Run = run;
				m_Cancel = CancellationSource{};
				Tools::GameExportRequest request;
				request.projectFile = m_Project;
				request.entrySource = m_Entry;
				request.output = m_RunningOutput;
				request.runtimeTemplate = executableDirectory / "RuntimeTemplate";
				request.toolResources = executableDirectory / "Resources";
				request.cancellation = m_Cancel.Token();
				request.progress = [this](std::string message) {
					std::scoped_lock lock(m_Mutex);
					m_Phase = std::move(message);
				};
				m_Status.clear();
				m_Job = std::async(std::launch::async, [request = std::move(request)] {
					std::string error;
					Tools::ExportWindowsGame(request, error);
					return error;
				});
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (busy) {
			if (ImGui::Button(UiText("export.cancel", "Cancel export")))
				m_Cancel.Cancel();
			std::scoped_lock lock(m_Mutex);
			ImGui::TextWrapped("%s", m_Phase.c_str());
		}
		if (!m_LastOutput.empty())
			ImGui::TextWrapped(
			    "%s: %s", UiText("export.last", "Last successful output"), Tools::AssetPathText(m_LastOutput).c_str());
		if (!m_Status.empty())
			ImGui::TextWrapped("%s", m_Status.c_str());
		ImGui::End();
	}
}
