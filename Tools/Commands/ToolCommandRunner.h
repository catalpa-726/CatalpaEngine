#pragma once
#include "catpch.h"

#include "Core/Jobs/CancellationToken.h"
namespace Catalpa::Tools {
	struct ToolCommandResult {
		int exitCode = 1;
		std::vector<std::string> diagnostics;
		std::vector<std::filesystem::path> outputs;
	};
	/* 可链接无设备工具入口；宿主只转交UTF-8参数，首项为命令而不是程序名。 */
	class ToolCommandRunner final {
	public:
		/**
		 * @brief 解析开发工具宿主的命令行并分派给对应的工具入口。
		 *
		 * @param[in] arguments 调用时传入的参数集合。
		 * @param[in] cancellation 用于查询取消请求的令牌。
		 *
		 * @return 工具命令的退出码、诊断与生成路径；0 表示成功，非零表示参数错误、执行失败或取消。
		 */
		static ToolCommandResult Run(std::span<const std::string> arguments, CancellationToken cancellation = {});
	};
}
