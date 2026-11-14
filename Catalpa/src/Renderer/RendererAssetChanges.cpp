#include "catpch.h"

#include "Renderer/Renderer.h"
#include "Assets/AssetManager.h"
#include "RHI/RHIDevice.h"

namespace Catalpa {
	/**
	 * @brief 刷新资产变更记录。
	 *
	 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
	 */
	RHIStatus Renderer::RefreshAssetChanges() {
		std::vector<AssetChangeEvent> changes;
		bool reset = false;
		std::string error;
		if (!m_Assets.ReadChanges(m_AssetChangeCursor, changes, reset, error))
			return {RHIErrorCode::InvalidState, 0, "Renderer/asset change journal"};
		if (reset) {
			m_Geometry.Invalidate({}, 0);
			m_Textures.Invalidate({}, 0);
		}
		for (const auto& change : changes) {
			m_Geometry.Invalidate(change.id, change.version);
			m_Textures.Invalidate(change.id, change.version);
		}
		/* 持有旧版本的引用继续指向原版本。释放空闲所有者后，仍按所有使用队列的完成点核算占用。 */
		m_Geometry.Prune();
		m_Textures.Prune();
		m_Compute.Prune();
		if (const auto status = m_Residency.MakeRoom(
		        0, [this](const auto& point, bool& complete) { return m_Device.QueryCompletion(point, complete); });
		    !status.IsSuccess())
			return status;
		const auto budget = m_Residency.Budget();
		m_Statistics.managedPayloadBytes = budget.used;
		m_Statistics.managedPayloadBudget = budget.limit;
		std::vector<RHIMemoryHeapBudget> heaps;
		if (const auto status = m_Device.GetMemoryBudgets(heaps); status.IsSuccess()) {
			m_Statistics.deviceAllocationBytes = m_Statistics.deviceBlockBytes = 0;
			m_Statistics.heapUsageBytes = m_Statistics.heapBudgetBytes = 0;
			m_Statistics.driverMemoryBudget = !heaps.empty();
			for (const auto& heap : heaps)
				if (heap.deviceLocal) {
					m_Statistics.deviceAllocationBytes += heap.allocationBytes;
					m_Statistics.deviceBlockBytes += heap.blockBytes;
					m_Statistics.heapUsageBytes += heap.usageBytes;
					m_Statistics.heapBudgetBytes += heap.budgetBytes;
					m_Statistics.driverMemoryBudget = m_Statistics.driverMemoryBudget && heap.driverBudget;
				}
		}
		return {};
	}
} /* 命名空间 Catalpa */
