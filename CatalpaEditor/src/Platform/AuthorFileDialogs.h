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
	 * @param[in] defaultExtension 作者文档筛选使用的扩展名；留空时采用所有作者文档类型。
	 * @param[in] allFiles 为真时不限制文件类型，供模型、图像等源资产导入使用。
	 *
	 * @return 选定文件或用户取消时为 true，两者由 cancelled 区分；对话框或路径转换错误时为 false。
	 */
	bool SelectAuthorFile(bool save, const std::filesystem::path& initialDirectory, std::filesystem::path& selected,
	    bool& cancelled, std::string& error, std::string_view defaultExtension = {}, bool allFiles = false);
	/**
	 * @brief 使用系统文件夹选择器选取项目父目录或待导入项目目录。
	 * @param[in] initialDirectory 初始目录，可为空。
	 * @param[out] selected 成功时接收用户选取的绝对目录。
	 * @param[out] cancelled 用户取消时设为 true。
	 * @param[out] error 原生对话框失败时接收原因。
	 * @return 对话框正常结束（包括取消）时为 true。
	 */
	bool SelectAuthorFolder(const std::filesystem::path& initialDirectory, std::filesystem::path& selected,
	    bool& cancelled, std::string& error);
}
