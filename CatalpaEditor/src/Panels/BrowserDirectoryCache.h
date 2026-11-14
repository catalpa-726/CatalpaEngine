#pragma once
#include "catpch.h"

namespace Catalpa::Editor {
	struct BrowserEntry {
		std::filesystem::path path;
		std::string name;
		bool folder = false;
	};

	struct BrowserListing {
		std::vector<BrowserEntry> entries;
		std::string error;
		bool limited = false;
	};

	/* 绑定到作者根目录的枚举结果。树和网格共享缓存；刷新后重新解析磁盘路径。 */
	class BrowserDirectoryCache {
	public:
		void Clear() {
			m_Listings.clear();
			m_Count = 0;
		}
		/**
		 * @brief 取得限定根目录下的文件夹清单。
		 *
		 * @param[in] canonicalRoot 规范化根。
		 * @param[in] directory 请求取得文件和子目录清单的目录路径。
		 *
		 * @return 共享的只读目录清单，包含可浏览条目、扫描截断标志及错误文本；缓存命中会复用已有清单。
		 */
		std::shared_ptr<const BrowserListing> List(
		    const std::filesystem::path& canonicalRoot, const std::filesystem::path& directory);

	private:
		std::map<std::filesystem::path, std::shared_ptr<const BrowserListing>> m_Listings;
		std::filesystem::path m_Root;
		size_t m_Count = 0;
	};
}
