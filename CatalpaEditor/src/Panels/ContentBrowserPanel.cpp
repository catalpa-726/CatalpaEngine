#include "catpch.h"

#include "Panels/ContentBrowserPanel.h"

#include "Panels/BuildPanel.h"
#include "Panels/FormLayout.h"
#include "Platform/AuthorFileDialogs.h"
#include <shlobj.h>
#include "AssetPipeline/AssetImportRegistry.h"
#include "Core/PlatformServices.h"
#include "imgui.h"
#include "Panels/EditorText.h"

namespace Catalpa::Editor {
	namespace {
		/** @brief 在 Windows 资源管理器中选中导入失败的外部源文件。 */
		bool RevealInFileManager(const std::filesystem::path& path, std::string& error) {
			const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
				error = "Cannot initialize the file manager";
				return false;
			}
			PIDLIST_ABSOLUTE item = nullptr;
			HRESULT status = SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr);
			if (SUCCEEDED(status)) {
				status = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
				CoTaskMemFree(item);
			}
			if (SUCCEEDED(initialized))
				CoUninitialize();
			if (FAILED(status))
				error = "Cannot locate the source file in Explorer";
			return SUCCEEDED(status);
		}
	}
	void ContentBrowserPanel::ClearSelection() {
		m_Selected.clear();
		m_SelectedEvent.clear();
		m_RemapUri = {};
		m_RemapTarget = {};
	}

	void ContentBrowserPanel::QueueImport(const std::vector<std::filesystem::path>& paths, ImportPlacement placement) {
		if (m_ImportApproved || !m_Imports.empty()) {
			m_Error = "Finish or cancel the pending import first";
			return;
		}
		m_ReviewJob.Cancel();
		m_ReviewJob = {};
		m_Review.reset();
		m_Imports = paths;
		m_Placement = std::move(placement);
		m_NextImport = 0;
		m_ImportApproved = false;
	}

	void ContentBrowserPanel::Reset() {
		ClearSelection();
		m_Directory.clear();
		m_Opened.clear();
		m_ImportGeneration = 0;
		m_ImportDirectory.clear();
		m_FailedSource.clear();
		m_Placement = {};
		m_RequestImport = false;
		m_ReviewJob.Cancel();
		m_ReviewJob = {};
		m_Review.reset();
		m_CreateType.clear();
		m_Imports.clear();
		m_NextImport = 0;
		m_ImportApproved = false;
		m_Browser.Reset();
		m_Error.clear();
		m_Root = 0;
	}

	void ContentBrowserPanel::AdvanceImport(
	    const Tools::ProjectSession& session, const std::filesystem::path& shaders, BuildPanel& build) {
		if (m_Imports.empty())
			return;
		const auto source = m_Imports[m_NextImport];
		Tools::AssetBuildRequest request{session.config, source, shaders};
		if (!m_ImportApproved) {
			const auto directory = m_Root == 0 && !m_Directory.empty() ? m_Directory : session.config.contentRoot;
			const auto* settings = build.ImportSettings(request, m_Error);
			if (!settings) {
				m_ReviewJob.Cancel();
				m_ReviewJob = {};
				m_Review.reset();
				m_FailedSource = source;
				return;
			}
			if (directory != m_ImportDirectory || *settings != m_ReviewedSettings) {
				m_ReviewJob.Cancel();
				m_ReviewJob = {};
				m_Review.reset();
				m_ImportDirectory = directory;
			}
			if (!m_Review) {
				m_ReviewedSettings = *settings;
				request.settings = m_ReviewedSettings;
				m_Review = std::make_shared<Tools::ProjectImportReview>();
				if (!build.ReviewImport(std::move(request), directory, m_Review, m_ReviewJob, m_Error))
					m_FailedSource = source;
			} else if (m_ReviewJob.IsComplete() && !m_ReviewJob.GetError().empty()) {
				m_FailedSource = source;
				m_Error = Tools::AssetPathText(source) + ": " + m_ReviewJob.GetError();
			}
			return;
		}
		if (build.Busy())
			return;
		if (!m_ImportGeneration) {
			request.settings = m_ReviewedSettings;
			if (build.Start(std::move(request), m_Error, m_ImportDirectory, m_Review))
				m_ImportGeneration = build.Generation();
			else
				m_ImportApproved = false;
			return;
		}
		bool success = build.Generation() == m_ImportGeneration && build.Ready() && build.Recipe();
		if (success) {
			const auto file = build.Recipe()->source;
			if (IsPathWithinRoot(file, session.config.contentRoot)) {
				m_Root = 0;
				m_Directory = file.parent_path();
			} else {
				size_t i = 0;
				for (const auto& [name, root] : session.config.externalSourceRoots) {
					++i;
					if (IsPathWithinRoot(file, root)) {
						m_Root = i;
						m_Directory = file.parent_path();
						break;
					}
				}
			}
			if (m_Selected != file) {
				m_RemapUri = {};
				m_RemapTarget = {};
			}
			m_Selected = file;
			if (!m_Placement)
				m_SelectedEvent = file;
			m_Browser.Reset();
			success = !m_Placement || m_Placement(build, file, m_Error);
		} else {
			m_FailedSource = source;
			m_Error = Tools::AssetPathText(source) + ": " +
			    (build.Diagnostic().empty() ? "Import cancelled or superseded" : build.Diagnostic());
		}
		if (!success || ++m_NextImport == m_Imports.size())
			m_Imports.clear();
		else {
			/* 批量导入逐份确认；默认沿用用户最初选定的目标，而非刚复制的模型目录。 */
			m_Root = 0;
			m_Directory = m_ImportDirectory;
		}
		m_ImportGeneration = 0;
		m_ImportApproved = false;
		m_ReviewJob = {};
		m_Review.reset();
	}

	void ContentBrowserPanel::Draw(bool& visible, const Tools::ProjectSession* session,
	    const std::filesystem::path& shaderRoot, BuildPanel& build, const PanelText& text) {
		if (session)
			AdvanceImport(*session, shaderRoot, build);
		if (!visible)
			return;
		if (ImGui::Begin(text("panel.content", "Content Browser", "Catalpa.Content").c_str(), &visible)) {
			if (!session)
				ImGui::TextWrapped("%s",
				    text("content.open_project", "Open an author project from File to select read-only source files.",
				        nullptr)
				        .c_str());
			else {
				std::vector<std::pair<std::string, std::filesystem::path>> roots{
				    {UiText("browser.content", "Content"), session->config.contentRoot}};
				for (const auto& [name, path] : session->config.externalSourceRoots)
					roots.push_back({name, path});
				if (m_Root >= roots.size())
					m_Root = 0;
				if (m_Directory.empty()) {
					m_Directory = roots[m_Root].second;
				}
				if (ImGui::BeginCombo(
				        text("content.root", "Read-only root", "ContentRoot").c_str(), roots[m_Root].first.c_str())) {
					for (size_t i = 0; i < roots.size(); ++i) {
						ImGui::PushID(int(i));
						if (ImGui::Selectable(roots[i].first.c_str(), m_Root == i)) {
							m_Root = i;
							m_Directory = roots[i].second;
							ClearSelection();
							m_Browser.Reset();
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				const bool requestImport = std::exchange(m_RequestImport, false);
				if (ImGui::Button(UiText("assetwork.import", "Import asset...")) || requestImport) {
					std::filesystem::path selected;
					bool cancelled = false;
					if (SelectAuthorFile(false, m_Directory, selected, cancelled, m_Error, {}, true) && !cancelled)
						QueueImport({std::move(selected)});
				}
				ImGui::SameLine();
				if (ImGui::Button(UiText("assetwork.create", "Create")))
					ImGui::OpenPopup("##CreateSource");
				if (ImGui::BeginPopup("##CreateSource")) {
					if (ImGui::MenuItem(UiText("ui.new_csharp_script", "New C# script")))
						m_CreateType = "script";
					if (ImGui::MenuItem(UiText("ui.new_behavior_graph", "New behavior graph")))
						m_CreateType = "behavior_graph";
					if (ImGui::MenuItem(UiText("ui.new_material", "New material")))
						m_CreateType = "material_instance";
					if (ImGui::MenuItem(UiText("ui.new_material_graph", "New material graph")))
						m_CreateType = "material_graph";
					ImGui::EndPopup();
				}
				if (!m_Imports.empty()) {
					ImGui::TextWrapped("%zu / %zu   %s", m_NextImport + 1, m_Imports.size(),
					    Tools::AssetPathText(m_Imports[m_NextImport]).c_str());
					if (m_ImportApproved && build.Busy())
						ImGui::ProgressBar(build.Progress());
					else if (m_ReviewJob.HasValue() && !m_ReviewJob.IsComplete())
						ImGui::ProgressBar(-float(ImGui::GetTime()), {-1, 0},
						    UiText("build.phase.checking", "Checking inputs and cache"));
					const bool reviewed = m_Review && m_ReviewJob.IsComplete() && m_ReviewJob.GetError().empty();
					if (reviewed) {
						ImGui::TextWrapped("%s: %s", UiText("assetwork.destination", "Import destination"),
						    Tools::AssetPathText(m_Review->destination).c_str());
						for (const auto& [file, hash] : m_Review->inputs)
							ImGui::TextWrapped("%s", Tools::AssetPathText(file).c_str());
						for (const auto& warning : m_Review->warnings)
							ImGui::TextWrapped("%s", warning.c_str());
					}
					ImGui::BeginDisabled(!reviewed || m_ImportApproved);
					if (ImGui::Button(m_Placement
					            ? UiText("assetwork.import_place", "Import and place in scene")
					            : UiText("assetwork.confirm_import", "Import source and dependencies"))) {
						m_ImportApproved = true;
						m_FailedSource.clear();
						m_Error.clear();
					}
					ImGui::EndDisabled();
					if (ImGui::Button(UiText("ui.cancel", "Cancel"))) {
						if (m_ImportGeneration && build.Generation() == m_ImportGeneration && build.Busy())
							build.Cancel();
						m_Imports.clear();
						m_ReviewJob.Cancel();
						m_ReviewJob = {};
						m_Review.reset();
						m_ImportGeneration = 0;
						m_ImportApproved = false;
					}
				}
				if (m_Browser.Draw(roots[m_Root].second, roots[m_Root].first, m_Directory, m_Selected,
				        ImGui::GetTextLineHeightWithSpacing() * 7, text)) {
					m_RemapUri = {};
					m_RemapTarget = {};
					m_Error.clear();
				}
				if (auto clicked = m_Browser.TakeClicked(); !clicked.empty())
					m_SelectedEvent = std::move(clicked);
				if (auto opened = m_Browser.TakeOpened(); !opened.empty()) {
					const auto ext = Tools::AssetSourceExtension(opened);
					if (ext == ".cs" || ext == ".lua" || ext == ".script" || ext == ".behavior" || ext == ".matgraph" ||
					    ext == ".prefab" || ext == ".scene" || ext == ".material")
						m_Opened = std::move(opened);
				}
				if (!build.Diagnostic().empty())
					ImGui::TextWrapped("%s", build.Diagnostic().c_str());
				if (!m_Error.empty()) {
					ImGui::TextWrapped("%s", m_Error.c_str());
					if (!m_FailedSource.empty() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
						for (size_t i = 0; i < roots.size(); ++i)
							if (IsPathWithinRoot(m_FailedSource, roots[i].second)) {
								m_Root = i;
								m_Directory = m_FailedSource.parent_path();
								if (m_Selected != m_FailedSource) {
									m_RemapUri = {};
									m_RemapTarget = {};
								}
								m_Selected = m_FailedSource;
								m_SelectedEvent = m_Selected;
								m_Browser.Reset();
								break;
							}
						if (!IsPathWithinRoot(m_FailedSource, roots[m_Root].second)) {
							std::string dialogError;
							if (!RevealInFileManager(m_FailedSource, dialogError))
								m_Error += "\n" + dialogError;
						}
					}
				}
			}
		}
		ImGui::End();
	}
	bool ContentBrowserPanel::DrawSourceInspector(const Tools::ProjectSession* session,
	    const std::filesystem::path& shaderRoot, BuildPanel& build, const PanelText& text) {
		if (!session || m_Selected.empty())
			return false;
		static const auto importers = Tools::AssetImportRegistry::Defaults();
		std::error_code fileError;
		if (!importers.Find(m_Selected) || !std::filesystem::is_regular_file(m_Selected, fileError))
			return true;
		Tools::AssetBuildRequest request{session->config, m_Selected, shaderRoot};
		auto* settings = build.ImportSettings(request, m_Error);
		if (!settings) {
			ImGui::TextWrapped("%s", m_Error.c_str());
			return true;
		}
		ImGui::BeginDisabled(build.Busy());
		PlaceFormLabel(text("content.unit_scale", "Additional unit scale", nullptr));
		ImGui::InputDouble("##ImportUnitScale", &settings->unitScale, 0, 0, "%.6g");
		const char* axes[]{UiText("content.up_auto", "Auto / source metadata"), "Y", "Z"};
		PlaceFormLabel(text("content.up_axis", "Source up axis", nullptr));
		int up = settings->sourceUp == "y" ? 1 : settings->sourceUp == "z" ? 2 : 0;
		if (ImGui::Combo("##ImportUpAxis", &up, axes, 3))
			settings->sourceUp = up == 1 ? "y" : up == 2 ? "z" : "auto";
		PlaceFormLabel(text("content.normals", "Generate missing face normals", nullptr), 2.f);
		ImGui::Checkbox("##GenerateNormals", &settings->generateNormals);
		PlaceFormLabel(text("content.tangents", "Generate missing MikkTSpace tangents", nullptr), 2.f);
		ImGui::Checkbox("##GenerateTangents", &settings->generateTangents);
		if (ImGui::TreeNode(text("content.remap", "Explicit dependency remaps", "DependencyRemaps").c_str())) {
			PlaceFormLabel(text("content.remap_uri", "Original URI", nullptr));
			ImGui::InputText("##RemapURI", m_RemapUri.data(), m_RemapUri.size());
			PlaceFormLabel(text("content.remap_target", "Replacement path", nullptr));
			ImGui::InputText("##RemapTarget", m_RemapTarget.data(), m_RemapTarget.size());
			if (ImGui::Button(text("content.remap_add", "Add / replace mapping", "AddRemap").c_str()) &&
			    m_RemapUri[0] && m_RemapTarget[0]) {
				if (settings->dependencyRemaps.contains(m_RemapUri.data()) || settings->dependencyRemaps.size() < 64)
					settings->dependencyRemaps.insert_or_assign(m_RemapUri.data(), m_RemapTarget.data());
				else
					m_Error = "At most 64 dependency remaps can be added";
			}
			if (ImGui::Button(text("content.remap_clear", "Clear mappings", "ClearRemaps").c_str()))
				settings->dependencyRemaps.clear();
			for (const auto& [uri, target] : settings->dependencyRemaps)
				ImGui::TextWrapped("%s -> %s", uri.c_str(), target.c_str());
			ImGui::TreePop();
		}
		if (ImGui::Button(text("content.build", "Build and load selected source", "BuildSource").c_str())) {
			request.settings = *settings;
			if (build.Start(std::move(request), m_Error))
				m_Error.clear();
		}
		ImGui::EndDisabled();
		if (!m_Error.empty())
			ImGui::TextWrapped("%s", m_Error.c_str());
		return true;
	}
}
