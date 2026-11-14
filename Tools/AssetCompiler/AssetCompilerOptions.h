#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
	/* UTF-8 参数适配不依赖具体宿主，所有路径显式传入，无须从 CatalpaBox 或进程工作目录推导。 */
	class AssetCompilerOptions final {
	public:
		/**
		 * @brief 解析资源构建的命令行参数并验证输入输出选项。
		 *
		 * @param[in] args 调用时传入的参数集合。
		 * @param[out] output 接收 AssetBuildRequest 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Parse(std::span<const std::string>, AssetBuildRequest&, std::string& error);
	};
}
