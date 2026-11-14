#pragma once
#include "catpch.h"

namespace Catalpa::Editor {
	/**
	 * @brief 弹出原生文件选择或保存对话框，区分选定、取消与错误。
	 * 原生对话框只返回路径和取消状态，加载/保存由文档服务校验。
	 *
	 * @param[in] save 为真时使用保存文件对话框。
	 * @param[in] initialDirectory 对话框起始目录。
	 * @param[out] selected 接收用户确认的文件路径。
	 * @param[out] cancelled 接收对话框是否由用户取消。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] defaultExtension 保存对话框使用的默认扩展名。
	 *
	 * @return 选定文件或用户取消时为 true，两者由 cancelled 区分；对话框或路径转换错误时为 false。
	 */
	bool SelectAuthorFile(bool save, const std::filesystem::path& initialDirectory, std::filesystem::path& selected,
	    bool& cancelled, std::string& error, std::string_view defaultExtension = {});
}
