#pragma once
#include "catpch.h"

#include "Core/Base.h"
namespace Catalpa {
	struct FontAsset {
		std::string sourceUri, family;
		std::vector<std::byte> bytes;
		uint32_t glyphCount = 0;
	};
	/**
	 * @brief 校验字体字节。
	 *
	 * @param[in] bytes 字节数据。
	 * @param[in] family 队列族或类型族标识。
	 * @param[in] glyphCount glyph 数量。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateFontBytes(
	    std::span<const std::byte>, std::string& family, uint32_t& glyphCount, std::string& error);
} /* 命名空间 Catalpa */
