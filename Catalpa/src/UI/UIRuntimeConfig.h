#pragma once
#include "Core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
namespace Catalpa {
	struct UIRuntimeConfig {
		float density = 1;
		uint32_t maxDocuments = 16;
	};
	/**
	 * @brief 读取界面运行时配置。
	 *
	 * @param[in] text 描述界面运行时设置的 JSON 文本。
	 * @param[out] out 接收 UIRuntimeConfig 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadUIRuntimeConfig(std::string_view, UIRuntimeConfig&, std::string& error);
} /* 命名空间 Catalpa */
