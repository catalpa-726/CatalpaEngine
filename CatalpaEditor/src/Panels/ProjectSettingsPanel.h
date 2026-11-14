#pragma once
#include "Project/ProjectConfig.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
	class EditorProject;
	class ProjectSettingsPanel {
	public:
		/**
		 * @brief 显示和编辑当前项目的构建及路径配置。
		 *
		 * @param[in] project 当前编辑的作者项目及持久化设置。
		 * @param[in] buildsBusy 忙碌状态。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void Draw(EditorProject&, bool buildsBusy, const PanelText&, std::string& error);
		/**
		 * @brief 清除面板持有的项目设置编辑状态。
		 */
		void Reset() {
			m_Project = {};
			m_Draft.reset();
			m_FieldOverflow = false;
		}

	private:
		StableId m_Project;
		std::optional<Tools::ProjectConfig> m_Draft;
		std::array<std::array<char, 2048>, 6> m_Fields{};
		bool m_FieldOverflow = false;
	};
}
