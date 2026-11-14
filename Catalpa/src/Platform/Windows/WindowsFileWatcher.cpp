#include "catpch.h"

#include "Core/IO/FileWatcher.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Catalpa::FileWatcherDetail {
	/**
	 * @brief 读取指定路径的文件属性并更新用于检测变更的时间戳记录。
	 *
	 * @param[in] path 待监视文件的路径，不接受目录。
	 * @param[out] stamp 成功时输出文件大小、最后写入时间和存在标记；路径不存在时输出空记录。
	 * @param[out] error 无法查询文件属性或路径为目录时写入原因。
	 *
	 * @return 成功读取文件戳或确认文件缺失时返回 true；目标为目录或属性读取失败时返回 false。
	 */
	bool Query(const std::filesystem::path& path, FileStamp& stamp, std::string& error) {
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
			const auto code = GetLastError();
			if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
				stamp = {};
				error.clear();
				return true;
			}
			error = "Cannot inspect watched input (Windows error " + std::to_string(code) + ")";
			return false;
		}
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			error = "Watch input must be a file";
			return false;
		}
		stamp = {uint64_t(data.nFileSizeHigh) << 32 | data.nFileSizeLow,
		    uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime, true};
		error.clear();
		return true;
	}
} /* 命名空间 Catalpa::FileWatcherDetail */
