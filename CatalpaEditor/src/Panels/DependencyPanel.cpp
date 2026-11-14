#include "catpch.h"

#include "Panels/DependencyPanel.h"

#include "imgui.h"

namespace Catalpa::Editor {
	void DependencyPanel::Select(AssetId id) {
		m_Selected = id;
		m_Direct = m_Graph.Dependencies(id);
		m_Affected = m_Graph.Affected(std::span(&id, 1));
	}

	void DependencyPanel::Draw(bool& visible, const Tools::AssetBuildResult& result, const PanelText& text) {
		if (!visible)
			return;
		if (ImGui::Begin(text("panel.dependencies", "Dependencies", "Catalpa.Dependencies").c_str(), &visible)) {
			if (!result.success)
				ImGui::TextWrapped("%s",
				    text("dependencies.empty", "Build an asset to inspect its dependency graph.", nullptr).c_str());
			else {
				if (m_Key != result.key) {
					m_Error.clear();
					if (m_Graph.Build(result.records, m_Error))
						Select(result.primary);
					m_Key = result.key;
				}
				if (!m_Error.empty())
					ImGui::TextWrapped("%s", m_Error.c_str());
				else {
					if (ImGui::BeginListBox("##DependencyAssets", {0, 140})) {
						ImGuiListClipper clipper;
						clipper.Begin(static_cast<int>(result.records.size()));
						while (clipper.Step())
							for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
								const auto& record = result.records[size_t(i)];
								ImGui::PushID(record.id.ToString().c_str());
								const auto position = ImGui::GetCursorScreenPos();
								if (ImGui::Selectable("##DependencyAsset", m_Selected == record.id))
									Select(record.id);
								const auto label = record.type.ToString() + " " + record.id.ToString();
								ImGui::GetWindowDrawList()->AddText(
								    position, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
								ImGui::PopID();
							}
						ImGui::EndListBox();
					}
					ImGui::SeparatorText(text("dependencies.direct", "Direct dependencies", nullptr).c_str());
					for (auto id : m_Direct)
						ImGui::TextUnformatted(id.ToString().c_str());
					ImGui::SeparatorText(
					    text("dependencies.affected", "Affected assets if selected changes", nullptr).c_str());
					for (auto id : m_Affected)
						ImGui::TextUnformatted(id.ToString().c_str());
				}
			}
		}
		ImGui::End();
	}
}
