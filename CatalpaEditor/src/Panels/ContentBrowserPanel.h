#pragma once
#include "Panels/FileBrowserView.h"
#include "Project/ProjectConfig.h"
#include "AssetPipeline/ProjectSourceImport.h"

namespace Catalpa::Editor {
	class BuildPanel;
	/** @brief 导入成功时，将刚构建的模型放入发起拖放的作者场景。 */
	using ImportPlacement = std::function<bool(BuildPanel&, const std::filesystem::path&, std::string&)>;

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

		/** @brief 取走一次单击的资源路径，重新点击同一文件也切回资源属性。 */
		std::filesystem::path TakeSelected() { return std::exchange(m_SelectedEvent, {}); }
		/** @return 当前选中的源文件路径，无选择时为空。 */
		const std::filesystem::path& SelectedPath() const noexcept { return m_Selected; }
		/**
		 * @brief 显示当前源文件的导入参数和重新构建操作。
		 * @param[in] session 提供源根与导入配置的作者项目。
		 * @param[in] shaderRoot 烹饪使用的着色器根目录。
		 * @param[in,out] build 接收重新构建请求的任务服务。
		 * @param[in] text 本地化回调。
		 * @return 已显示选中源文件时为 true。
		 */
		bool DrawSourceInspector(const Tools::ProjectSession* session, const std::filesystem::path& shaderRoot,
		    BuildPanel& build, const PanelText& text);
		/** @return 本次双击且具有作者编辑器的源文件，取走后清空。 */
		std::filesystem::path TakeOpened() { return std::exchange(m_Opened, {}); }
		/** @return 取走项目资源菜单选择的新建文档类型。 */
		std::string TakeCreate() { return std::exchange(m_CreateType, {}); }
		/**
		 * @brief 排队接收系统拖入的文件，用户在内容浏览器确认后依次导入。
		 * @param[in] paths 用户选择的源文件列表。
		 * @param[in] placement 导入成功时的可选场景放置函数。
		 */
		void QueueImport(const std::vector<std::filesystem::path>& paths, ImportPlacement placement = {});
		/** @brief 在内容浏览器下次绘制时打开原生导入对话框。 */
		void RequestImport() noexcept { m_RequestImport = true; }

	private:
		std::filesystem::path m_Opened, m_SelectedEvent;
		std::vector<std::filesystem::path> m_Imports;
		size_t m_NextImport = 0;
		uint64_t m_ImportGeneration = 0;
		std::filesystem::path m_ImportDirectory, m_FailedSource;
		bool m_ImportApproved = false, m_RequestImport = false;
		ImportPlacement m_Placement;
		JobHandle m_ReviewJob;
		std::shared_ptr<Tools::ProjectImportReview> m_Review;
		Tools::AssetImportSettings m_ReviewedSettings;
		void AdvanceImport(const Tools::ProjectSession&, const std::filesystem::path& shaders, BuildPanel&);
		std::string m_CreateType;
		void ClearSelection();
		std::filesystem::path m_Directory, m_Selected;
		FileBrowserView m_Browser{0x434f4e54454e5410ULL};
		std::string m_Error;
		size_t m_Root = 0;
		std::array<char, 4096> m_RemapUri{}, m_RemapTarget{};
	};
}
