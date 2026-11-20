#pragma once
#include <filesystem>
#include <string>

namespace Catalpa::Editor {
	/** @brief 创建新的 C# 行为源码；已存在的文件由调用方另选名称。 */
	bool CreateBehaviourFile(const std::filesystem::path& script, const std::filesystem::path& root,
	    std::string& error);
	/** @brief 使用工作区选定的本机 IDE 打开源文件，并在可用时加载脚本项目。 */
	bool LaunchScriptEditor(const std::string& preference, const std::filesystem::path& script,
	    const std::filesystem::path& project, std::string& error);
}
