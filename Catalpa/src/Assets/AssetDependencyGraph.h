#pragma once
#include "Core/Base.h"
#include "Assets/AssetRecord.h"

namespace Catalpa {
	/* 逻辑资产依赖的确定拓扑和反向闭包，与GPU RenderGraph无关。 */
	class CATALPA_API AssetDependencyGraph final {
	public:
		/**
		 * @brief 根据资产记录建立正向依赖和反向依赖索引。
		 *
		 * @param[in] records 用于构图的资产记录集合。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Build(std::span<const AssetRecord> records, std::string& error);

		const std::vector<AssetId>& Order() const { return m_Order; }

		std::vector<AssetId> Dependencies(AssetId id) const;
		/**
		 * @brief 从变更集合沿反向依赖遍历，收集受影响的资产。
		 *
		 * @param[in] changed 作为遍历起点的变更资产标识。
		 *
		 * @return 变更标识与所有可达反向依赖方的有序去重列表。
		 */
		std::vector<AssetId> Affected(std::span<const AssetId> changed) const;

	private:
		std::map<AssetId, std::vector<AssetId>> m_Forward, m_Reverse;
		std::vector<AssetId> m_Order;
	};
}
