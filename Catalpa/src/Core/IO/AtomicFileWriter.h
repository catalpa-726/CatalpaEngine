#pragma once

#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	/*
	 * 仅用于显式保存作者文档或派生产物，可覆盖的目标由调用者指定。
	 * 目标须位于已存在的绝对目录中；此接口不创建目录或修改原始输入。
	 */
	class CATALPA_API AtomicFileWriter {
	public:
		/**
		 * @brief 在目标所在目录独占创建临时文件，写入并刷新后，通过一次重命名提交。
		 * 提交前失败时，目标文件保持原样，仅清理本次创建的临时文件。
		 * 调用期间须保持祖先目录结构稳定。重命名成功不保证所有文件系统在断电后都能保留此次更新。
		 *
		 * @param[in] absoluteTarget 要创建或替换的目标文件绝对路径，父目录须已存在。
		 * @param[in] bytes 按原顺序写入临时文件的字节范围，允许包含零字节。
		 * @param[out] error 返回 false 时接收失败原因，例如目录校验、写入或重命名失败。
		 *
		 * @return 目标替换成功时为 true；写入或提交失败时为 false。
		 */
		[[nodiscard]] static bool Write(
		    const std::filesystem::path& absoluteTarget, std::span<const std::byte> bytes, std::string& error);
		/**
		 * @brief 将文本的原始字节原子写入目标文件，不做编码转换，也不追加终止字符。
		 *
		 * @param[in] absoluteTarget 要创建或替换的绝对路径；其父目录须已存在。
		 * @param[in] text 待写入的文本视图，按视图中的字节数写入，可包含零字节。
		 * @param[out] error 失败时写入目录校验、文件写入或重命名的诊断。
		 *
		 * @return 目标文件替换成功时为 true，写入或提交失败时为 false。
		 */
		[[nodiscard]] static bool Write(
		    const std::filesystem::path& absoluteTarget, std::string_view text, std::string& error);
	};
}
