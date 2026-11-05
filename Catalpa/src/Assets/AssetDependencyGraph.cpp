#include "catpch.h"
#include "Assets/AssetDependencyGraph.h"

namespace Catalpa {
    bool AssetDependencyGraph::Build(std::span<const AssetRecord> records, std::string& error) {
        try {
            AssetDependencyGraph candidate;
            if (records.size() > 65536) {
                error = "Dependency graph exceeds 65536 assets";
                return false;
            }
            for (const auto& record : records)
                if (!record.id.HasValue() ||
                    !candidate.m_Forward.emplace(record.id, record.dependencies).second) {
                    error = "Duplicate/invalid asset in dependency graph";
                    return false;
                }
            std::map<AssetId, size_t> remaining;
            std::set<AssetId> ready;
            for (const auto& [id, dependencies] : candidate.m_Forward) {
                std::set<AssetId> unique;
                for (auto dependency : dependencies) {
                    if (dependency == id || !candidate.m_Forward.contains(dependency) ||
                        !unique.insert(dependency).second) {
                        error = "Invalid, missing or repeated dependency " + dependency.ToString() + " of " +
                                id.ToString();
                        return false;
                    }
                    candidate.m_Reverse[dependency].push_back(id);
                }
                remaining[id] = dependencies.size();
                if (dependencies.empty())
                    ready.insert(id);
            }
            // 只发布依赖已齐的记录；多个记录同时就绪时，按稳定 ID 产生确定顺序。
            while (!ready.empty()) {
                const auto id = *ready.begin();
                ready.erase(ready.begin());
                candidate.m_Order.push_back(id);
                const auto found = candidate.m_Reverse.find(id);
                if (found != candidate.m_Reverse.end()) {
                    for (auto parent : found->second)
                        if (--remaining.at(parent) == 0)
                            ready.insert(parent);
                }
            }
            if (candidate.m_Order.size() != records.size()) {
                error = "Asset dependency cycle";
                return false;
            }
            m_Forward.swap(candidate.m_Forward);
            m_Reverse.swap(candidate.m_Reverse);
            m_Order.swap(candidate.m_Order);
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    std::vector<AssetId> AssetDependencyGraph::Dependencies(AssetId id) const {
        const auto i = m_Forward.find(id);
        return i == m_Forward.end() ? std::vector<AssetId>{} : i->second;
    }

    std::vector<AssetId> AssetDependencyGraph::Affected(std::span<const AssetId> changed) const {
        std::set<AssetId> visited(changed.begin(), changed.end());
        std::vector<AssetId> queue(visited.begin(), visited.end());
        for (size_t i = 0; i < queue.size(); ++i) {
            const auto found = m_Reverse.find(queue[i]);
            if (found == m_Reverse.end())
                continue;
            for (auto parent : found->second)
                if (visited.insert(parent).second)
                    queue.push_back(parent);
        }
        return {visited.begin(), visited.end()};
    }
}
