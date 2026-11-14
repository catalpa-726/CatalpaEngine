#pragma once
#include "imgui.h"

#include <algorithm>
#include <initializer_list>
#include <string>

namespace Catalpa::Editor {
	enum class ToolIcon { Select, Move, Rotate, Scale, Grid, Snap, Reset, Stats, Play, Pause, Step, Stop };

	/**
	 * @brief 绘制有选中状态和悬停提示的视口工具按钮。
	 *
	 * @param[in] id 工具按钮的 ImGui 标识，用于区分相邻按钮。
	 * @param[in] icon 按钮中绘制的工具图标。
	 * @param[in] tooltip 指针悬停时显示的功能说明。
	 * @param[in] selected 当前工具已激活时使用选中色。
	 *
	 * @return 本帧触发该按钮时为 true。
	 */
	inline bool ToolbarButton(const char* id, ToolIcon icon, const std::string& tooltip, bool selected = false) {
		const float side = ImGui::GetFrameHeight();
		if (selected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
		const bool pressed = ImGui::Button(id, {side, side});
		if (selected)
			ImGui::PopStyleColor();
		const ImVec2 min = ImGui::GetItemRectMin();
		const float r = side * .29f;
		const ImVec2 center(min.x + side * .5f, min.y + side * .5f);
		const auto pos = [&](float x, float y) { return ImVec2(center.x + x * r, center.y + y * r); };
		auto* draw = ImGui::GetWindowDrawList();
		const auto color = ImGui::GetColorU32(icon == ToolIcon::Play ? ImGuiCol_CheckMark : ImGuiCol_Text);
		const float stroke = std::max(1.2f, side * .055f);
		const auto line = [&](float x, float y, float u, float v) {
			draw->AddLine(pos(x, y), pos(u, v), color, stroke);
		};
		switch (icon) {
		case ToolIcon::Select:
			draw->AddTriangleFilled(pos(-.65f, -.95f), pos(-.46f, .75f), pos(.68f, .25f), color);
			line(-.15f, .2f, .35f, .95f);
			break;
		case ToolIcon::Move:
			line(-1, 0, 1, 0);
			line(0, -1, 0, 1);
			for (float sign : {-1.f, 1.f}) {
				line(sign, 0, sign * .6f, -.25f);
				line(sign, 0, sign * .6f, .25f);
				line(0, sign, -.25f, sign * .6f);
				line(0, sign, .25f, sign * .6f);
			}
			break;
		case ToolIcon::Rotate:
		case ToolIcon::Reset:
			draw->PathArcTo(center, .76f * r, -.9f, 4.5f, 22);
			draw->PathStroke(color, 0, stroke);
			line(.58f, -.58f, .7f, -.95f);
			line(.58f, -.58f, .12f, -.63f);
			if (icon == ToolIcon::Reset)
				draw->AddCircleFilled(center, r * .18f, color);
			break;
		case ToolIcon::Scale:
			draw->AddRect(pos(-.76f, -.76f), pos(.76f, .76f), color);
			draw->AddRectFilled(pos(-1, -1), pos(-.52f, -.52f), color);
			draw->AddRectFilled(pos(.52f, .52f), pos(1, 1), color);
			break;
		case ToolIcon::Grid:
			draw->AddRect(pos(-.9f, -.9f), pos(.9f, .9f), color);
			for (float v : {-.3f, .3f}) {
				line(v, -.9f, v, .9f);
				line(-.9f, v, .9f, v);
			}
			break;
		case ToolIcon::Snap:
			draw->PathArcTo(pos(0, .1f), r * .65f, 0, 3.14159f, 12);
			draw->PathStroke(color, 0, stroke * 1.5f);
			line(-.65f, .12f, -.65f, -.8f);
			line(.65f, .12f, .65f, -.8f);
			break;
		case ToolIcon::Stats:
			line(-.75f, .75f, -.75f, .15f);
			line(0, .75f, 0, -.75f);
			line(.75f, .75f, .75f, -.2f);
			break;
		case ToolIcon::Play:
		case ToolIcon::Step:
			draw->AddTriangleFilled(pos(-.65f, -.8f), pos(-.65f, .8f), pos(.6f, 0), color);
			if (icon == ToolIcon::Step)
				line(.83f, -.8f, .83f, .8f);
			break;
		case ToolIcon::Pause:
			draw->AddRectFilled(pos(-.68f, -.75f), pos(-.2f, .75f), color);
			draw->AddRectFilled(pos(.2f, -.75f), pos(.68f, .75f), color);
			break;
		case ToolIcon::Stop:
			draw->AddRectFilled(pos(-.7f, -.7f), pos(.7f, .7f), color);
			break;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", tooltip.c_str());
		return pressed;
	}
}
