#pragma once
#include "Panels/FileBrowserView.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
	class BuildPanel;

	/* 只读作者文件选择和导入设置；构建任务由工作区长期持有的BuildPanel管理。 */
	class ContentBrowserPanel final {
	public:
		/**
		 * @brief 重置浏览器、当前选择及导入选项。
		 */
		void Reset();
		void Update() { m_Browser.Update(); }
		/**
		 * @brief 绘制只读源文件浏览和导入选项，并将构建操作交给构建面板。
		 *
		 * @param[in] visible 控制可见性的标志。
		 * @param[in] session 提供可浏览源根和导入设置的作者项目会话，可为空。
		 * @param[in] shaderRoot 着色器文件所在的根目录。
		 * @param[in] build 接收用户提交的源文件导入请求并显示构建结果的面板。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void Draw(bool& visible, const Tools::ProjectSession*, const std::filesystem::path& shaderRoot, BuildPanel&,
		    const PanelText&);

	private:
		void ClearSelection();
		std::filesystem::path m_Directory, m_Selected;
		FileBrowserView m_Browser{0x434f4e54454e5410ULL};
		std::string m_Error;
		size_t m_Root = 0;
		double m_UnitScale = 1;
		int m_Up = 0;
		bool m_Normals = true, m_Tangents = true;
		std::array<char, 4096> m_RemapUri{}, m_RemapTarget{};
		std::map<std::string, std::string, std::less<>> m_Remaps;
	};
}
