#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
	/* 配置 JSON 的有界适配；读写失败均保留调用者原输出。 */
	class CATALPA_API JsonArchive final {
	public:
		/**
		 * @brief 在给定预算内将 JSON 解析为独占的归档值树。
		 * 解析和结果分配全部在候选内完成，成功只交换独占结果；失败保留旧指针/值树。
		 *
		 * @param[in] text 待解析的 JSON 文本。
		 * @param[out] output 成功时接收解析出的归档树。
		 * @param[out] error 失败时记录归档节点的路径和具体原因。
		 * @param[in] limits 限制输入大小、节点数、容器大小和嵌套深度的预算。
		 *
		 * @return 归档树完全解析并发布时为 true，否则为 false 并保留旧输出。
		 */
		static bool Parse(std::string_view text, std::unique_ptr<ArchiveValue>& output, ArchiveError& error,
		    const ArchiveLimits& limits = {});
		/**
		 * @brief 在指定预算内将归档树输出为 JSON。
		 *
		 * @param[in] value 等待序列化的归档树。
		 * @param[out] output 成功时接收生成的 JSON 文本。
		 * @param[in] error 失败时记录归档节点的路径和具体原因。
		 * @param[in] limits 编码时适用的节点数、字符串和容器等限制。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Write(
		    const ArchiveValue& value, std::string& output, ArchiveError& error, const ArchiveLimits& limits = {});
	};
}
