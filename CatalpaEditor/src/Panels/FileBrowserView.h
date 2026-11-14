#pragma once

#include "Panels/PanelText.h"
#include "Panels/BrowserDirectoryCache.h"
#include "Panels/BrowserThumbnails.h"

namespace Catalpa::Editor {
	/* 项目 Assets 与导入浏览器使用相同的导航规则，导入选项仍由调用者管理。 */
	class FileBrowserView {
	public:
		explicit FileBrowserView(uint64_t id) : m_Thumbnails(id) {}
		/**
		 * @brief 清除当前根目录的列表、过滤状态和缩略图缓存。
		 */
		void Reset();
		void Update() { m_Thumbnails.BeginFrame(); }
		/**
		 * @brief 在给定根目录内绘制目录树和文件列表，更新浏览位置及选择。
		 *
		 * @param[in] root 根目录路径。
		 * @param[in] rootLabel 面包屑导航和目录树根节点显示的名称。
		 * @param[in,out] directory 当前打开的目录，发生跳转时更新。
		 * @param[in,out] selected 当前选中项的路径，选择变化时更新。
		 * @param[in] height 文件夹与文件列表面板的显示高度。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 *
		 * @return 本帧打开的目录或选中的文件发生变化时为 true。
		 */
		bool Draw(const std::filesystem::path& root, std::string_view rootLabel, std::filesystem::path& directory,
		    std::filesystem::path& selected, float height, const PanelText& text);

	private:
		void DrawFolder(const std::filesystem::path&, std::string_view, const std::filesystem::path& current,
		    std::filesystem::path& navigate, unsigned depth, unsigned& budget);
		std::filesystem::path m_Root;
		BrowserDirectoryCache m_Folders;
		std::array<char, 128> m_Filter{};
		bool m_Grid = true;
		BrowserThumbnails m_Thumbnails;
	};
}
