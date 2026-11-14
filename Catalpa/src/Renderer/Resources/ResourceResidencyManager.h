#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Core/Memory/MemoryBudget.h"
#include "Assets/AssetId.h"
#include "RHI/RHICompletionSet.h"

namespace Catalpa {
	struct ResourceResidencyKey {
		AssetId asset;
		uint64_t version = 0;
		uint32_t subresource = 0;
		auto operator<=>(const ResourceResidencyKey&) const = default;
	};
	/* 受管payload预算与LRU；不把估算payload字节冒称驱动/进程的总显存。 */
	class CATALPA_API ResourceResidencyManager final {
	public:
		using CompletionQuery = std::function<RHIStatus(const RHICompletionPoint&, bool&)>;
		explicit ResourceResidencyManager(size_t payloadBudget = 512 * 1024 * 1024, size_t maxEntries = 8192);
		/**
		 * @brief 将资产版本和载荷大小计入住留预算，并登记驱逐条件。
		 *
		 * @param[in] key 包含资产标识、版本和用途的键。
		 * @param[in] bytes 计入预算的载荷字节数。
		 * @param[in] mayEvict 检查持有方能否放弃该资源的回调。
		 * @param[in] evict 真正移除资源时执行的回调。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Track(
		    ResourceResidencyKey, size_t bytes, std::function<bool()> mayEvict, std::function<void()> evict);
		/**
		 * @brief 将新受理的完成点并入住留条目的使用记录，供后续安全驱逐。
		 * 预留完成点容量发生在Track；已接受提交后的Touch不分配。
		 *
		 * @param[in] key 本次使用的驻留资源键。
		 * @param[in] points 本次使用所关联的队列完成点。
		 *
		 * @return 驻留条目已登记且本次使用的全部完成点能够记录时返回 true。
		 */
		bool Touch(ResourceResidencyKey, std::span<const RHICompletionPoint>) noexcept;
		/**
		 * @brief 按使用先后检查可驱逐资源，只在相关 GPU 提交均已完成时回收。
		 *
		 * @param[in] requiredBytes 需要留出的载荷字节数，零表示只清理待回收项。
		 * @param[in] query 查询设备与队列完成点的回调。
		 *
		 * @return 返回驻留容量回收结果；仍被引用或使用的资源无法腾出足够空间时返回容量错误。
		 */
		RHIStatus MakeRoom(size_t requiredBytes, const CompletionQuery&);
		/**
		 * @brief 失效版本的所有者可暂留缓存供复用，待外部引用和所有相关队列的使用都结束后再淘汰。
		 *
		 * @param[in] key 请求清理的驻留资源键。
		 */
		void RequestEviction(ResourceResidencyKey) noexcept;
		/**
		 * @brief owner已释放仍保留账目，直到MakeRoom核实所有已接受使用的完成点。
		 *
		 * @param[in] key 已由其持有方释放的资源键。
		 */
		void Forget(ResourceResidencyKey) noexcept;
		/**
		 * @brief 调用者先停止使用并等待/失设备收束；此接口本身不制造GPU完成证明。
		 */
		void ClearAfterDeviceStopped() noexcept;
		MemoryBudgetStats Budget() const noexcept;
		size_t Size() const noexcept;

	private:
		struct Entry {
			size_t bytes = 0;
			uint64_t age = 0;
			bool ownerReleased = false, poisoned = false, evictionRequested = false;
			RHICompletionSet uses;
			std::function<bool()> mayEvict;
			std::function<void()> evict;
		};
		bool Owner() const noexcept;
		size_t m_Limit, m_MaxEntries, m_Used = 0, m_Peak = 0, m_Rejected = 0;
		uint64_t m_Age = 0;
		std::thread::id m_Owner;
		std::map<ResourceResidencyKey, Entry> m_Entries;
	};
} /* 命名空间 Catalpa */
