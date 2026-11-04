#include "catpch.h"
#include "Assets/AssetRegistry.h"

namespace Catalpa {
    namespace {

        // 这里只校验声明格式；实际内容 SHA-256 校验由读取/构建链执行。
        bool IsContentHashValid(std::string_view hash) noexcept
        {
            if (hash.empty()) return true;
            if (hash.size() != 64) return false;
            for (char c : hash) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        }

    }

    bool AssetRegistry::Replace(const std::vector<AssetRecord>& records, std::string& error)
    {
        try {
            // 所有分配、校验和诊断构造发生在候选值中，最终交换不再分配。
            auto candidateRecords = records;
            std::map<AssetId, size_t> candidateIds;
            std::map<std::string, size_t, std::less<>> candidatePaths;
            std::vector<AssetDependencyIssue> candidateMissing;
            for (size_t i = 0; i < candidateRecords.size(); ++i) {
                const auto& record = candidateRecords[i];
                const auto fail = [&](std::string_view detail) {
                    error = "AssetRegistry record[" + std::to_string(i) + "]: " + std::string(detail);
                    return false;
                };
                if (!record.id.HasValue()) return fail("asset ID is invalid");
                if (!record.type.HasValue()) return fail("asset type is invalid");
                if (!record.locator.HasValue()) return fail("virtual locator or location kind is invalid");
                if (record.version == 0) return fail("content version must be nonzero");
                if (!IsContentHashValid(record.contentHash)) return fail("contentHash must be empty or a 64-character lowercase SHA-256");
                if (!candidateIds.emplace(record.id, i).second) return fail("duplicate asset ID");
                // 地址使用 VirtualPath 的规范原文；不对 Unicode 或文件名做隐式大小写归并。
                if (!candidatePaths.emplace(record.locator.path.ToString(), i).second) return fail("duplicate virtual address");
                std::set<AssetId> dependencies;
                for (const auto dependency : record.dependencies) {
                    if (!dependency.HasValue()) return fail("dependency ID is invalid");
                    if (dependency == record.id) return fail("asset cannot depend on itself");
                    if (!dependencies.insert(dependency).second) return fail("duplicate dependency ID");
                }
            }
            // 必须在全部记录索引建成后判断缺失，结果顺序稳定且不依赖输入记录的前后可见性。
            for (const auto& record : candidateRecords) {
                for (const auto dependency : record.dependencies) {
                    if (!candidateIds.contains(dependency)) candidateMissing.push_back({ record.id, dependency });
                }
            }
            m_Records.swap(candidateRecords);
            m_ById.swap(candidateIds);
            m_ByPath.swap(candidatePaths);
            m_MissingDependencies.swap(candidateMissing);
            error.clear();
            return true;
        }
        catch (const std::bad_alloc&) { error = "AssetRegistry allocation failed; previous records retained"; return false; }
        catch (const std::length_error&) { error = "AssetRegistry capacity exceeded; previous records retained"; return false; }
    }

    const AssetRecord* AssetRegistry::Find(AssetId id) const noexcept
    {
        if (!id.HasValue()) return nullptr;
        const auto found = m_ById.find(id);
        return found != m_ById.end() ? &m_Records[found->second] : nullptr;
    }

    const AssetRecord* AssetRegistry::Find(const VirtualPath& path) const noexcept
    {
        if (!path.HasValue()) return nullptr;
        const auto found = m_ByPath.find(path.ToString());
        return found != m_ByPath.end() ? &m_Records[found->second] : nullptr;
    }

    const AssetRecord* AssetRegistry::Find(const AssetLocator& locator) const noexcept
    {
        if (!locator.HasValue()) return nullptr;
        const auto* record = Find(locator.path);
        return record && record->locator.kind == locator.kind ? record : nullptr;
    }

}
