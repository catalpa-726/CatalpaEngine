#include "catpch.h"

#include "Core/IO/FileWatcher.h"

namespace Catalpa {
	namespace FileWatcherDetail {
		bool Query(const std::filesystem::path&, FileStamp&, std::string&);
	}

	bool FileWatcher::SetPaths(std::span<const std::filesystem::path> paths, std::string& error) {
		try {
			if (paths.size() > 4096) {
				error = "Watch input count exceeds 4096";
				return false;
			}
			std::map<std::filesystem::path, Watched> candidate;
			for (const auto& path : paths) {
				if (!path.is_absolute()) {
					error = "Watch paths must be absolute";
					return false;
				}
				FileStamp stamp;
				if (!FileWatcherDetail::Query(path, stamp, error))
					return false;
				candidate.emplace(path.lexically_normal(), Watched{stamp, {}, false});
			}
			m_Files.swap(candidate);
			m_NextPoll = {};
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}

	bool FileWatcher::Poll(std::vector<std::filesystem::path>& changed, std::string& error) {
		const auto now = std::chrono::steady_clock::now();
		changed.clear();
		if (now < m_NextPoll) {
			error.clear();
			return true;
		}
		m_NextPoll = now + std::chrono::milliseconds(250);
		for (auto& [path, watched] : m_Files) {
			FileStamp current;
			if (!FileWatcherDetail::Query(path, current, error))
				return false;
			if (current != watched.observed) {
				watched.observed = current;
				watched.changedAt = now;
				watched.dirty = true;
			} else if (watched.dirty && now - watched.changedAt >= std::chrono::milliseconds(500)) {
				changed.push_back(path);
				watched.dirty = false;
			}
		}
		error.clear();
		return true;
	}
} /* 命名空间 Catalpa */
