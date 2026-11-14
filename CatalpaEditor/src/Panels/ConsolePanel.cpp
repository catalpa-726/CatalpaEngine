#include "Panels/EditorText.h"
#include "catpch.h"

#include "Panels/ConsolePanel.h"

#include "imgui.h"

namespace Catalpa::Editor {
	void ConsolePanel::Update() {
		if (m_Paused)
			return;
		uint64_t dropped = 0;
		auto records = m_Subscription.Drain(256, dropped);
		m_Dropped += dropped;
		for (auto& record : records) {
			m_Records.push_back(std::move(record));
			if (m_Records.size() > 2048) {
				m_Records.pop_front();
				++m_Dropped;
			}
		}
	}

	void ConsolePanel::Draw(bool& visible, const PanelText& text) {
		if (!visible)
			return;
		if (ImGui::Begin(text("panel.console", "Console", "Catalpa.Console").c_str(), &visible)) {
			if (ImGui::Button(text("console.clear", "Clear", "ClearConsole").c_str())) {
				m_Records.clear();
				m_Subscription.Clear();
				m_Dropped = 0;
			}
			ImGui::SameLine();
			ImGui::Checkbox(text("console.pause", "Pause", "PauseConsole").c_str(), &m_Paused);
			ImGui::SameLine();
			ImGui::Checkbox(text("console.follow", "Follow", "FollowConsole").c_str(), &m_Follow);
			ImGui::SetNextItemWidth(140);
			ImGui::Combo(text("console.level", "Minimum level", "ConsoleLevel").c_str(), &m_MinLevel,
			    UiChoices({UiText("choice.trace", "Trace"), UiText("choice.debug", "Debug"),
			                  UiText("choice.info", "Info"), UiText("choice.warning", "Warning"),
			                  UiText("choice.error", "Error"), UiText("choice.critical", "Critical")})
			        .c_str());
			ImGui::InputText(
			    text("assets.filter", "Filter", "ConsoleFilter").c_str(), m_Filter.data(), m_Filter.size());
			if (m_Dropped)
				ImGui::Text(UiFormat("console.dropped", "Dropped/evicted records: %llu"),
				    static_cast<unsigned long long>(m_Dropped));
			ImGui::BeginChild("##ConsoleLines", {0, 0}, ImGuiChildFlags_Borders);
			const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
			for (const auto& record : m_Records) {
				if (record.level < m_MinLevel || record.message.find(m_Filter.data()) == std::string::npos)
					continue;
				ImGui::PushStyleColor(ImGuiCol_Text,
				    record.level >= 4       ? ImVec4(1, .4f, .4f, 1)
				        : record.level == 3 ? ImVec4(1, .8f, .4f, 1)
				                            : ImGui::GetStyleColorVec4(ImGuiCol_Text));
				ImGui::TextWrapped("[%llu] %s: %s", static_cast<unsigned long long>(record.sequence),
				    record.logger.c_str(), record.message.c_str());
				ImGui::PopStyleColor();
			}
			if (m_Follow && atBottom)
				ImGui::SetScrollHereY(1);
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
