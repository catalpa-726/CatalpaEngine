#pragma once

#include "imgui.h"

#include <string_view>

namespace Catalpa::Editor {
	/**
	 * @brief 将阻塞工作区的模态框放在主原生窗口内，首次显示时居中。
	 * 拖出的普通面板仍可独立呈现；模态框不另开系统窗口，避免被宿主遮挡而只留下不可点击的遮罩。
	 */
	inline void PrepareEditorModal() {
		const auto* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {.5f, .5f});
	}

	/**
	 * @brief 按实际字体和当前面板宽度放置字段标题，为紧接其后的控件预留输入空间。
	 *
	 * @param[in] label 仅供显示的本地化标题；控件 ID 由调用方独立指定。
	 * @param[in] minInputEm 保持输入内容可操作所需的最少字号宽度。
	 */
	inline void PlaceFormLabel(std::string_view label, float minInputEm = 7.f) {
		const float available = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float labelWidth = ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;
		const float column = available * .42f;
		const bool inlineValue =
		    labelWidth <= column && available - column - spacing >= minInputEm * ImGui::GetFontSize();
		const float start = ImGui::GetCursorPosX();
		if (inlineValue)
			ImGui::AlignTextToFramePadding();
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextUnformatted(label.data(), label.data() + label.size());
		ImGui::PopTextWrapPos();
		if (inlineValue)
			ImGui::SameLine(start + column + spacing);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}
}
