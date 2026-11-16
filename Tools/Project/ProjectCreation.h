#pragma once
#include "Project/ProjectConfig.h"

namespace Catalpa::Tools {
	/**
	 * @brief 在既有目录中新建独立项目，并生成已登记的初始场景。
	 * @param[in] parent 用户选定的既有父目录，必须为绝对路径。
	 * @param[in] name 新项目和新子目录名称，不允许覆盖既有目录。
	 * @param[out] projectFile 成功时接收新项目文件的规范绝对路径。
	 * @param[out] error 失败时接收诊断，既有目录内容不受影响。
	 * @return 项目全部文件校验并提交成功时为 true。
	 */
	bool CreateProject(const std::filesystem::path& parent, std::string_view name, std::filesystem::path& projectFile,
	    std::string& error);

	/**
	 * @brief 只在所选项目根目录查找作者项目文件，不递归扫描磁盘。
	 * @param[in] directory 用户选择的目录。
	 * @param[out] candidates 成功时接收排序后的项目文件列表；允许多个候选。
	 * @param[out] error 目录无效或扫描失败时接收诊断。
	 * @return 扫描成功时为 true；空结果由调用方解释为缺少项目描述。
	 */
	bool FindProjectFiles(
	    const std::filesystem::path& directory, std::vector<std::filesystem::path>& candidates, std::string& error);
}
