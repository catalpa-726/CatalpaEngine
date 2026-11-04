#pragma once

#include "catpch.h"
#include "Assets/AssetId.h"

namespace Catalpa {

    // 仅保存逻辑身份；复制 handle 不会加载资产，也不会使任一旧内容版本继续存活。
    template<typename T>
    struct AssetHandle {
        AssetId id;

        constexpr AssetHandle() = default;
        explicit constexpr AssetHandle(AssetId asset) noexcept : id(asset) {}
        [[nodiscard]] constexpr bool HasValue() const noexcept { return id.HasValue(); }
        constexpr auto operator<=>(const AssetHandle&) const = default;
    };

    // 拥有一个固定内容版本的只读载荷；热重载不会自动改变现有引用。
    // 调用方必须保证载荷及其传递引用不再被可写别名修改，shared_ptr<const T> 不宣称任意 T 深度不可变。
    // 当前基础值不执行加载；后续 AssetManager 负责受检发布，不在此引入 GPU 状态或运行期槽位编号。
    template<typename T>
    class AssetVersionRef {
    public:
        [[nodiscard]] bool HasValue() const noexcept { return m_Value != nullptr; }
        [[nodiscard]] AssetId GetId() const noexcept { return m_Id; }
        [[nodiscard]] uint64_t GetVersion() const noexcept { return m_Version; }
        [[nodiscard]] const T* Get() const noexcept { return m_Value.get(); }
        [[nodiscard]] const std::shared_ptr<const T>& GetShared() const noexcept { return m_Value; }

        // 只在有效身份、非零内容版本和非空载荷同时成立时发布；失败不改变输出引用。
        [[nodiscard]] static bool TryCreate(AssetId id, uint64_t version,
            std::shared_ptr<const T> value, AssetVersionRef& output, std::string& error) {
            if (!id.HasValue()) { error = "Asset version requires a valid asset ID"; return false; }
            if (version == 0) { error = "Asset version must be nonzero"; return false; }
            if (!value) { error = "Asset version requires a non-null immutable payload"; return false; }
            AssetVersionRef candidate;
            candidate.m_Id = id;
            candidate.m_Version = version;
            candidate.m_Value = std::move(value);
            output = std::move(candidate);
            error.clear();
            return true;
        }

    private:
        AssetId m_Id;
        uint64_t m_Version = 0;
        std::shared_ptr<const T> m_Value;
    };

}
