#pragma once

#include "Core/Base.h"
#include "Core/Id/StableId.h"

namespace Catalpa {
	/**
	 * @brief 使用系统随机源生成新的稳定 UUID。
	 * 新作者身份使用系统随机UUIDv4；加载/重新cook保留原身份，失败不发布部分值。
	 *
	 * @param[out] output 成功时接收新生成的非零稳定标识。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功写出非零 UUIDv4 时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool GenerateStableId(StableId& output, std::string& error);
}
