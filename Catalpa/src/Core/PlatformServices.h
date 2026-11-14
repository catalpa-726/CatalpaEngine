#pragma once

#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	/**
	 * @brief 定位当前宿主的部署根，不从工作目录或沙盒名称推导路径；失败保留 output。
	 *
	 * @param[out] output 成功时接收当前可执行文件所在目录的绝对路径。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 已定位当前可执行文件目录时返回 true，失败时保留原输出。
	 */
	[[nodiscard]] CATALPA_API bool GetExecutableDirectory(std::filesystem::path& output, std::string& error);
	/**
	 * @brief 系统随机源，单次至多1MiB；成功才覆盖调用方缓冲区，不退回时间/计数伪随机。
	 *
	 * @param[out] output 接收系统随机源生成的原始字节；长度上限 1 MiB，失败保留原值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 系统随机源已填满输出缓冲区时返回 true，失败时保留原字节。
	 */
	[[nodiscard]] CATALPA_API bool FillRandomBytes(std::span<std::byte> output, std::string& error);
	/**
	 * @brief 内容身份使用真实SHA-256；失败不发布部分摘要，空输入亦有确定摘要。
	 *
	 * @param[in] input 按原顺序参与 SHA-256 计算的源字节。
	 * @param[out] output 成功时写入 32 字节 SHA-256 摘要，失败时保留原值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 完整计算并写入 SHA-256 摘要时返回 true；失败时保留原输出。
	 */
	[[nodiscard]] CATALPA_API bool ComputeSha256(
	    std::span<const std::byte> input, std::array<std::byte, 32>& output, std::string& error);
	/**
	 * @brief 返回含程序名的UTF-8参数，避免作者项目路径经过ANSI argv丢失Unicode字符。
	 *
	 * @param[out] output 成功时接收包含程序名在内的 UTF-8 参数列表，并保留空参数。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 已将含程序名的完整 UTF-8 参数序列写入 output 时返回 true。
	 */
	[[nodiscard]] CATALPA_API bool GetProcessArguments(std::vector<std::string>& output, std::string& error);
	/**
	 * @brief 逐组件比较已规范化的绝对路径，含根组件。Windows 按序数规则忽略大小写。
	 * 比较过程不解析重解析点。调用方须先解析真实路径，VFS 另行校验最终句柄。
	 *
	 * @param[in] candidate 待比较的绝对路径，不允许含点或双点路径段。
	 * @param[in] root 用于逐段比较的绝对根目录，Windows 上按不区分大小写的方式比较。
	 *
	 * @return 已规范化候选路径等于根路径或处于其下时返回 true。
	 */
	[[nodiscard]] CATALPA_API bool IsPathWithinRoot(
	    const std::filesystem::path& candidate, const std::filesystem::path& root);
}
