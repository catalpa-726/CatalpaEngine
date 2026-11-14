#pragma once

#include "catpch.h"

#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
	/* 编辑器仅持作者会话；通用解析/挂载/身份注册仍由可独立调用的 Tools 完成。 */
	class EditorProject final {
	public:
		bool Load(const std::filesystem::path& absoluteProjectFile, std::string& error);
		/**
		 * @brief 将当前作者项目的配置写回项目文件。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Save(std::string& error) const;
		/**
		 * @brief 应用配置。
		 *
		 * @param[in] candidate 待检查的 Tools::ProjectConfig。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ApplyConfig(const Tools::ProjectConfig&, std::string& error);
		[[nodiscard]] const Tools::ProjectSession* GetSession() const noexcept { return m_Session.get(); }
		void Close() noexcept { m_Session.reset(); }

	private:
		/* 关闭会话前须取消并等待仍使用它的构建任务。 */
		std::unique_ptr<Tools::ProjectSession> m_Session;
	};
}
