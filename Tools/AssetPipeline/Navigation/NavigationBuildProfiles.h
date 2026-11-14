#pragma once
#include "Navigation/NavMeshAsset.h"

namespace Catalpa::Tools {
	/**
	 * @brief 合并导航构建配置与作者覆盖值，解析导航源。
	 * profile是构建输入，调用者必须把配置字节计入DDC依赖；作者settings逐项显式覆盖。
	 *
	 * @param[in] source 记录导航几何及构建选项的 JSON 源文本。
	 * @param[in] profiles 定义可选导航构建配置及默认项的 JSON 文本。
	 * @param[out] output 成功时接收应用配置选择及作者覆盖值后的导航构建输入。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 导航源及其构建配置解析为有效的构建输入时为 true。
	 */
	bool ResolveNavigationBuildSource(
	    std::string_view source, std::string_view profiles, NavigationBuildInput&, std::string& error);
} /* 命名空间 Catalpa::Tools */
