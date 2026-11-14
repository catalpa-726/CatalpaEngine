#pragma once
#include "Scripting/ScriptValue.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
	/**
	 * @brief 编码脚本值。
	 *
	 * @param[in] values 将写入归档的命名脚本值集合。
	 * @param[out] output 接收 ArchiveValue 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool EncodeScriptValues(const ScriptValues&, ArchiveValue&, std::string& error);
	/**
	 * @brief 解码脚本值。
	 *
	 * @param[in] archive 要读取或转换的归档值。
	 * @param[out] output 接收脚本值结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool DecodeScriptValues(const ArchiveValue&, ScriptValues&, std::string& error);
}
