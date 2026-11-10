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

    // 绑定到作者根目录的枚举结果。树和网格共享缓存；刷新后重新解析磁盘路径。
    class BrowserDirectoryCache {
      public:
        void Clear() { m_Listings.clear(); m_Count = 0; }
        std::shared_ptr<const BrowserListing> List(const std::filesystem::path& canonicalRoot,
                                                   const std::filesystem::path& directory);
      private:
        std::map<std::filesystem::path, std::shared_ptr<const BrowserListing>> m_Listings;
        std::filesystem::path m_Root;
        size_t m_Count = 0;
    };
}
