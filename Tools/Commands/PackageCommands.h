#pragma once
#include "Commands/ToolCommandRunner.h"
namespace Catalpa::Tools {
	/**
	 * @brief 运行资源包命令。
	 *
	 * @param[in] args 调用时传入的参数集合。
	 * @param[in] cancellation 用于查询取消请求的令牌。
	 *
	 * @return 包含执行诊断和产物路径的命令结果。成功退出码为 0，参数错误为 2，取消为 130，普通执行错误为 1。
	 */
	ToolCommandResult RunPackageCommand(std::span<const std::string>, CancellationToken);
}
