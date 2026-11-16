#pragma once

#include "Commands/ToolCommandRunner.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Tools {
	/** @brief 描述从作者工程生成 Windows 游戏的输入。 */
	struct GameExportRequest {
		std::filesystem::path projectFile, entrySource, output, toolResources, runtimeTemplate;
		CancellationToken cancellation;
		std::function<void(std::string)> progress;
	};

	/**
	 * @brief 在独立派生目录重新构建场景、检查包闭包并一次性发布运行目录。
	 * @param[in] request 工程、入口源、模板与目标目录；目标必须尚不存在。
	 * @param[out] error 具体阶段和失败原因。
	 * @return 完整目录已成功发布时为 true。
	 */
	bool ExportWindowsGame(const GameExportRequest& request, std::string& error);
	/**
	 * @brief 将无窗口发布命令的显式参数传递给编辑器共用的发布流程。
	 * @param[in] args 含命令名的 UTF-8 参数。
	 * @param[in] cancellation 本次构建的取消令牌。
	 * @return 退出码、诊断及发布位置。
	 */
	ToolCommandResult RunGameExport(std::span<const std::string> args, CancellationToken cancellation);
}
