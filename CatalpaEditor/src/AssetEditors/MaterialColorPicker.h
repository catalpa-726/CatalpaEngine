#pragma once

#include <array>
#include <string>
#include <string_view>

namespace Catalpa::Editor {
	/** @brief 将线性材质颜色编码为 sRGB 十六进制预览，非全不透明时包含 Alpha。 */
	std::string FormatMaterialHex(const std::array<float, 4>& linear);

	/**
	 * @brief 解码 sRGB 十六进制颜色；六位输入保留原始 Alpha，八位输入覆盖 Alpha。
	 * @param[in] hex 带或不带 # 的六个或八个十六进制数字。
	 * @param[in,out] linear 成功后更新线性 RGB 及指定的 Alpha；失败时保持原值。
	 * @return 全部字符合法且长度正确时为 true。
	 */
	bool ParseMaterialHex(std::string_view hex, std::array<float, 4>& linear);

	/** @brief 管理一次选色弹窗的临时值，应用时一次提交，取消时丢弃。 */
	class MaterialColorPicker {
	public:
		/**
		 * @brief 绘制色样按钮和 sRGB 色板，提供 RGB、HEX 与线性 RGBA 编辑。
		 * @param[in,out] linear 文档中已校验的线性 RGBA。调用者需为文档和参数提供稳定的 ImGui ID。
		 * @param[in] hasAlpha 是否允许调节第四通道。仅 RGB 的自发光色样固定为不透明。
		 * @param[in] maximumRgb 线性 RGB 上限，PBR 反射率取 1，自发光与无光照颜色取 1e8。
		 * @return 显式应用了新的有效颜色时为 true，供调用者生成单条撤销命令。
		 */
		bool Draw(std::array<float, 4>& linear, bool hasAlpha = true, float maximumRgb = 1e8f);

	private:
		void SyncFields();
		std::array<float, 4> m_Color{};
		std::array<int, 4> m_Bytes{};
		std::array<char, 64> m_Hex{};
		bool m_HexValid = true;
	};
}
