#pragma once
#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	struct FileStamp {
		uint64_t size = 0, modified = 0;
		bool exists = false;
		bool operator==(const FileStamp&) const = default;
	};
	/*
	 * 只观察显式输入清单，有界轮询及静默窗口合并；不递归扫描、不持后台回调。
	 * 文件通知只是重导入提示，真正发布前仍由 AssetBuildContext 复核内容哈希。
	 */
	class CATALPA_API FileWatcher final {
	public:
		/**
		 * @brief 设置路径。
		 *
		 * @param[in] paths 参与操作的路径集合。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetPaths(std::span<const std::filesystem::path>, std::string& error);
		/**
		 * @brief 轮询已登记的文件，将本次发生变化的路径收集到输出。
		 *
		 * @param[out] changed 接收这一轮变化的路径。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Poll(std::vector<std::filesystem::path>& changed, std::string& error);
		bool HasInputs() const noexcept { return !m_Files.empty(); }
		void Clear() noexcept { m_Files.clear(); }

	private:
		struct Watched {
			FileStamp observed;
			std::chrono::steady_clock::time_point changedAt;
			bool dirty = false;
		};
		std::map<std::filesystem::path, Watched> m_Files;
		std::chrono::steady_clock::time_point m_NextPoll{};
	};
} /* 命名空间 Catalpa */
