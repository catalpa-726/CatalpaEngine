#pragma once
#include "Project/ProjectConfig.h"
#include "Core/Jobs/CancellationToken.h"
#include <future>

namespace Catalpa::Editor {
	/** @brief 编辑器的 Windows 发布窗口；后台工作只拥有本次导出的路径快照。 */
	class ExportPanel final {
	public:
		/** @brief 再次打开时使用当前作者场景作为入口，正在执行的导出保持其快照。 */
		void Open() noexcept {
			if (!m_Visible && !m_Job.valid())
				m_UseSaved = true;
			m_Visible = true;
		}
		/** @brief 取消并等待持有窗口进度回调的后台任务。 */
		~ExportPanel();
		/**
		 * @brief 选择入口和目录，提交或取消发布，并展示结果。
		 * @param[in] session 当前作者项目，可以为空。
		 * @param[in] executableDirectory 开发宿主目录。
		 * @param[in] sceneSource 当前场景已保存的源路径，可以为空。
		 * @param[in] dirty 当前作者场景是否存在尚未保存的改动。
		 * @param[in] saveScene 保存当前场景的编辑器操作，成功后下一帧接收新路径。
		 */
		void Draw(const Tools::ProjectSession* session, const std::filesystem::path& executableDirectory,
		    const std::filesystem::path& sceneSource, bool dirty, const std::function<bool()>& saveScene);

	private:
		bool m_Visible = false, m_Run = false, m_UseSaved = false;
		std::filesystem::path m_Project, m_Entry, m_Parent, m_LastOutput, m_RunningOutput;
		std::array<char, 160> m_Folder{"WindowsGame"};
		std::future<std::string> m_Job;
		CancellationSource m_Cancel;
		std::string m_Status, m_Phase;
		std::mutex m_Mutex;
	};
}
