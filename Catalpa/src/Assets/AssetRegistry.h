#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetRecord.h"

namespace Catalpa {

    // 尚未登记的依赖仍保留原 ID，供作者工具给出确定的缺失诊断。
    struct AssetDependencyIssue {
        AssetId asset;
        AssetId dependency;
    };

    // 同步元数据索引；配置线程负责修改，读取期间不允许并发 Replace。
    // 不创建领域资源、不解析 JSON，不将元数据存在等同于可加载或 GPU 已就绪。
    class CATALPA_API AssetRegistry {
    public:
        AssetRegistry() = default;
        AssetRegistry(const AssetRegistry&) = default;
        // 移动后源表为空；赋值先完整构造候选，防止分配失败留下记录/索引不一致的半张表。
        // 空索引的构造也可能分配；失败须向上传播，不能以noexcept转成进程终止。
        AssetRegistry(AssetRegistry&& other) { Swap(other); }
        AssetRegistry& operator=(AssetRegistry other) noexcept { Swap(other); return *this; }

        // 整表验证成功后一次替换；失败保留记录、索引、缺失诊断及已有查询指针。
        // 允许非自引用的依赖缺失，由 GetMissingDependencies 报告，不阻止检查不完整项目。
        [[nodiscard]] bool Replace(const std::vector<AssetRecord>& records, std::string& error);

        // 返回注册表拥有的只读记录；指针仅在本表下一次成功替换/赋值/析构前有效。
        [[nodiscard]] const AssetRecord* Find(AssetId id) const noexcept;
        [[nodiscard]] const AssetRecord* Find(const VirtualPath& path) const noexcept;
        [[nodiscard]] const AssetRecord* Find(const AssetLocator& locator) const noexcept;
        [[nodiscard]] const std::vector<AssetRecord>& GetRecords() const noexcept { return m_Records; }
        [[nodiscard]] const std::vector<AssetDependencyIssue>& GetMissingDependencies() const noexcept { return m_MissingDependencies; }

    private:
        void Swap(AssetRegistry& other) noexcept {
            m_Records.swap(other.m_Records);
            m_ById.swap(other.m_ById);
            m_ByPath.swap(other.m_ByPath);
            m_MissingDependencies.swap(other.m_MissingDependencies);
        }

        std::vector<AssetRecord> m_Records;
        std::map<AssetId, size_t> m_ById;
        std::map<std::string, size_t, std::less<>> m_ByPath;
        std::vector<AssetDependencyIssue> m_MissingDependencies;
    };

}
