#include "AssetEditors/MaterialColorPicker.h"

#include "Panels/EditorText.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Catalpa::Editor {
	namespace {
		/* 着色器读取线性 RGB；屏幕色样、RGB 字节和 HEX 使用 sRGB。Alpha 始终为线性覆盖率。
		 * 超出显示范围的通道只在生成预览时限制，打开弹窗和编辑其他通道均保留其原始精度。 */
		float ToDisplay(float linear) {
			if (!(linear > 0.f))
				return 0.f;
			if (linear >= 1.f)
				return 1.f;
			return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
		}

		float ToLinear(float srgb) {
			return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
		}

		std::array<float, 4> DisplayColor(const std::array<float, 4>& linear) {
			return {ToDisplay(linear[0]), ToDisplay(linear[1]), ToDisplay(linear[2]),
			    std::isfinite(linear[3]) ? std::clamp(linear[3], 0.f, 1.f) : 0.f};
		}

		std::array<int, 4> DisplayBytes(const std::array<float, 4>& linear) {
			const auto display = DisplayColor(linear);
			std::array<int, 4> bytes{};
			for (size_t i = 0; i < display.size(); ++i)
				bytes[i] = int(std::lround(display[i] * 255.f));
			return bytes;
		}

		int HexDigit(char c) {
			if (c >= '0' && c <= '9')
				return c - '0';
			if (c >= 'a' && c <= 'f')
				return c - 'a' + 10;
			if (c >= 'A' && c <= 'F')
				return c - 'A' + 10;
			return -1;
		}

		bool ValidLinear(const std::array<float, 4>& linear, float maximumRgb) {
			return std::all_of(linear.begin(), linear.begin() + 3,
			           [maximumRgb](float v) { return v >= 0 && v <= maximumRgb; }) &&
			    linear[3] >= 0.f && linear[3] <= 1.f;
		}
	}

	std::string FormatMaterialHex(const std::array<float, 4>& linear) {
		const auto bytes = DisplayBytes(linear);
		char out[10]{};
		std::snprintf(out, sizeof(out), "#%02X%02X%02X%02X", unsigned(bytes[0]), unsigned(bytes[1]), unsigned(bytes[2]),
		    unsigned(bytes[3]));
		return std::string(out, linear[3] == 1.f ? 7 : 9);
	}

	bool ParseMaterialHex(std::string_view hex, std::array<float, 4>& linear) {
		if (hex.starts_with('#'))
			hex.remove_prefix(1);
		if (hex.size() != 6 && hex.size() != 8)
			return false;
		auto next = linear;
		for (size_t i = 0; i < hex.size() / 2; ++i) {
			const int hi = HexDigit(hex[i * 2]), lo = HexDigit(hex[i * 2 + 1]);
			if (hi < 0 || lo < 0)
				return false;
			const float channel = float(hi * 16 + lo) / 255.f;
			next[i] = i == 3 ? channel : ToLinear(channel);
		}
		linear = next;
		return true;
	}

	void MaterialColorPicker::SyncFields() {
		m_Bytes = DisplayBytes(m_Color);
		const auto hex = FormatMaterialHex(m_Color);
		std::snprintf(m_Hex.data(), m_Hex.size(), "%s", hex.c_str());
		m_HexValid = true;
	}

	bool MaterialColorPicker::Draw(std::array<float, 4>& linear, bool hasAlpha, float maximumRgb) {
		const auto display = DisplayColor(linear);
		const float em = ImGui::GetFontSize();
		const bool open = ImGui::ColorButton("##MaterialColor", {display[0], display[1], display[2], display[3]},
		    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, {2.5f * em, ImGui::GetFrameHeight()});
		ImGui::SetItemTooltip("%s", UiText("material.color_edit", "Choose color"));
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(FormatMaterialHex(linear).c_str());
		if (open) {
			m_Color = linear;
			SyncFields();
			ImGui::OpenPopup("MaterialColorPopup");
		}
		const auto area = ImGui::GetWindowViewport()->WorkSize;
		ImGui::SetNextWindowSize({std::min(em * 17.f, area.x), 0}, ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints({0, 0}, {area.x, area.y});
		if (!ImGui::BeginPopup("MaterialColorPopup"))
			return false;
		/* 只实时更新弹窗的临时值，让输入校验和应用按钮在同一帧看到用户正在填写的内容。 */
		ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInput, true);
		ImGui::PushItemWidth(-FLT_MIN);
		auto selected = DisplayColor(m_Color);
		const auto pickerFlags = ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoInputs |
		    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions |
		    ImGuiColorEditFlags_NoAlpha;
		if (ImGui::ColorPicker4("##Palette", selected.data(), pickerFlags)) {
			/* 色板选择的是完整的显示色，主动选色时替换三个 RGB 通道，保留原始不透明度。 */
			for (size_t i = 0; i < 3; ++i)
				m_Color[i] = ToLinear(selected[i]);
			SyncFields();
		}

		ImGui::TextUnformatted(UiText("material.color_rgb", "RGB (sRGB 0–255)"));
		const auto originalBytes = DisplayBytes(m_Color);
		bool bytesEdited = ImGui::InputInt3("##MaterialRGB", m_Bytes.data());
		if (hasAlpha) {
			ImGui::TextUnformatted(UiText("material.color_alpha", "Opacity (0–255)"));
			bytesEdited |= ImGui::InputInt("##MaterialOpacity", &m_Bytes[3], 0, 0);
		}
		const bool validBytes = std::all_of(m_Bytes.begin(), m_Bytes.end(), [](int v) { return v >= 0 && v <= 255; });
		if (bytesEdited && validBytes) {
			for (size_t i = 0; i < 4; ++i)
				if (m_Bytes[i] != originalBytes[i]) {
					const float channel = float(m_Bytes[i]) / 255.f;
					m_Color[i] = i == 3 ? channel : ToLinear(channel);
				}
			SyncFields();
		}
		if (!validBytes)
			ImGui::TextWrapped("%s", UiText("material.color_rgb_invalid", "Enter integers from 0 to 255."));

		ImGui::TextUnformatted("HEX");
		if (ImGui::InputTextWithHint(
		        "##MaterialHex", hasAlpha ? "#RRGGBB / #RRGGBBAA" : "#RRGGBB", m_Hex.data(), m_Hex.size())) {
			const std::string_view hex(m_Hex.data());
			m_HexValid = (hasAlpha || hex.size() == (hex.starts_with('#') ? 7u : 6u)) && ParseMaterialHex(hex, m_Color);
			if (m_HexValid)
				m_Bytes = DisplayBytes(m_Color);
		}
		if (!m_HexValid)
			ImGui::TextWrapped("%s",
			    hasAlpha ? UiText("material.color_hex_invalid", "Enter #RRGGBB or #RRGGBBAA.")
			             : UiText("material.color_rgb_hex_invalid", "Enter #RRGGBB."));

		const auto linearLabel = hasAlpha ? UiLabel("material.color_linear", "Linear RGBA (raw values)")
		                                  : UiLabel("material.color_linear_rgb", "Linear RGB");
		if (ImGui::TreeNode(linearLabel.c_str())) {
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputScalarN("##MaterialLinear", ImGuiDataType_Float, m_Color.data(), hasAlpha ? 4 : 3, nullptr,
			        nullptr, "%.7g") &&
			    ValidLinear(m_Color, maximumRgb))
				SyncFields();
			ImGui::TreePop();
		}
		if (!ValidLinear(m_Color, maximumRgb)) {
			const char* invalid = maximumRgb == 1.f
			    ? UiText("material.color_reflectance_invalid", "PBR base RGB and opacity must be in [0,1].")
			    : hasAlpha ? UiText("material.color_linear_invalid", "RGB: 0–1e8; alpha: 0–1.")
			               : UiText("material.color_rgb_linear_invalid", "Linear RGB must be in [0,1e8].");
			ImGui::TextWrapped("%s", invalid);
		}
		ImGui::PopItemWidth();
		ImGui::PopItemFlag();

		bool changed = false;
		ImGui::BeginDisabled(!m_HexValid || !validBytes || !ValidLinear(m_Color, maximumRgb));
		if (ImGui::Button(UiLabel("material.color_apply", "Apply color").c_str())) {
			changed = linear != m_Color;
			linear = m_Color;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(UiLabel("material.color_cancel", "Cancel").c_str()))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return changed;
	}
}
