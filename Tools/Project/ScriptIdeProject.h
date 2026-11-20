#pragma once

#include "Project/ProjectConfig.h"

namespace Catalpa::Tools {
	/**
	 * @brief 在派生数据目录生成供 IDE 使用的单脚本项目。
	 * @param[in] project 提供工作区的派生数据根目录。
	 * @param[in] script 已存在的 C# 源文件。
	 * @param[in] apiAssembly 当前宿主部署的 Catalpa.Scripting.dll。
	 * @param[out] ideProject 成功时返回生成的项目路径。
	 * @param[out] error 失败时返回路径或写入诊断。
	 * @return 项目已存在且内容一致，或已成功写入时为 true。
	 */
	bool PrepareScriptIdeProject(const ProjectConfig& project, const std::filesystem::path& script,
	    const std::filesystem::path& apiAssembly, std::filesystem::path& ideProject, std::string& error);
}
