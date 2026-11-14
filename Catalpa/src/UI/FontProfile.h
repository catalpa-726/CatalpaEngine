#pragma once
#include "Core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
namespace Catalpa {
	struct FontProfile {
		std::string previewText;
		uint32_t previewPixelSize = 24, previewWidth = 640, previewHeight = 256;
	};
	/**
	 * @brief 读取字体配置方案。
	 *
	 * @param[in] text 包含预览文字、像素字号及预览画布尺寸的 JSON 文本。
	 * @param[out] output 接收 FontProfile 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadFontProfile(std::string_view, FontProfile&, std::string& error);
} /* 命名空间 Catalpa */
