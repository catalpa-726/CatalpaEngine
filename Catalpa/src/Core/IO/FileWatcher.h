#pragma once
#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    struct FileStamp {
        uint64_t size = 0, modified = 0;
        bool exists = false;
        bool operator==(const FileStamp&) const = default;
    };
    // 只观察显式输入清单，有界轮询及静默窗口合并；不递归扫描、不持后台回调。
    // 文件通知只是重导入提示，真正发布前仍由 AssetBuildContext 复核内容哈希。
    class CATALPA_API FileWatcher final {
      public:
        bool SetPaths(std::span<const std::filesystem::path>, std::string& error);
        bool Poll(std::vector<std::filesystem::path>& changed, std::string& error);
        bool HasInputs() const noexcept {
            return !m_Files.empty();
        }
        void Clear() noexcept {
            m_Files.clear();
        }

      private:
        struct Watched {
            FileStamp observed;
            std::chrono::steady_clock::time_point changedAt;
            bool dirty = false;
        };
        std::map<std::filesystem::path, Watched> m_Files;
        std::chrono::steady_clock::time_point m_NextPoll{};
    };
} // namespace Catalpa
