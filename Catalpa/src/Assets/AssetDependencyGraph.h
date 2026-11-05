#pragma once
#include "Core/Base.h"
#include "Assets/AssetRecord.h"

namespace Catalpa {
    // 逻辑资产依赖的确定拓扑和反向闭包，与GPU RenderGraph无关。
    class CATALPA_API AssetDependencyGraph final {
    public:
        bool Build(std::span<const AssetRecord> records, std::string& error);

        const std::vector<AssetId>& Order() const {
            return m_Order;
        }

        std::vector<AssetId> Dependencies(AssetId id) const;
        std::vector<AssetId> Affected(std::span<const AssetId> changed) const;

    private:
        std::map<AssetId, std::vector<AssetId>> m_Forward, m_Reverse;
        std::vector<AssetId> m_Order;
    };
}
