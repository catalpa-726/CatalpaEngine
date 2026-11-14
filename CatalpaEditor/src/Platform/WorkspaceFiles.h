#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Catalpa::Editor {

	/**
	 * @brief 确定当前可执行文件所在的目录。
	 * 返回实际可执行文件目录；设置与语言资源由调用方拼接，均不依赖工作目录。
	 *
	 * @param[out] executableDir 成功时接收当前进程可执行文件的父目录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功解析实际可执行文件的父目录时为 true。
	 */
	[[nodiscard]] bool GetWorkspacePaths(std::filesystem::path& executableDir, std::string& error);

	/**
	 * @brief 在字节限制内完整读取工作区文本。
	 * 有界读取完整文件；失败保留 text，不修改源文件。
	 *
	 * @param[in] path 要读入的工作区文本文件路径。
	 * @param[out] text 成功时接收整个文件内容；读取失败时保留原文本。
	 * @param[in] maxBytes 允许完整读入的最大文件字节数，在分配目标缓冲区前检查。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 在字节上限内读完整个文件时为 true，文件不可读或过大时为 false。
	 */
	[[nodiscard]] bool ReadWorkspaceText(
	    const std::filesystem::path& path, std::string& text, std::size_t maxBytes, std::string& error);

	/**
	 * @brief 先刷新临时文件，再备份旧配置并替换目标文件。
	 * 同目录写临时文件并刷新，再备份旧文件至 .bak、替换目标；失败保留原配置。
	 *
	 * @param[in] path 要创建或替换的工作区文本文件路径。
	 * @param[in] text 要完整写入临时文件并发布到目标路径的文本字节。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 临时文件刷新并成功安装为目标文件时为 true，创建、写入或替换失败时为 false。
	 */
	[[nodiscard]] bool WriteWorkspaceTextAtomic(
	    const std::filesystem::path& path, std::string_view text, std::string& error);

}
