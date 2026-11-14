#include "catpch.h"

#include "Renderer/Resources/ResourceResidencyManager.h"

namespace Catalpa {
	ResourceResidencyManager::ResourceResidencyManager(size_t bytes, size_t entries)
	    : m_Limit(bytes), m_MaxEntries(entries), m_Owner(std::this_thread::get_id()) {}

	bool ResourceResidencyManager::Owner() const noexcept {
		return m_Owner == std::this_thread::get_id();
	}

	MemoryBudgetStats ResourceResidencyManager::Budget() const noexcept {
		return {m_Limit, m_Used, m_Peak, m_Rejected};
	}

	size_t ResourceResidencyManager::Size() const noexcept {
		return m_Entries.size();
	}

	RHIStatus ResourceResidencyManager::Track(
	    ResourceResidencyKey key, size_t bytes, std::function<bool()> mayEvict, std::function<void()> evict) try {
		if (!Owner() || !key.asset.HasValue() || !key.version || !bytes || !mayEvict || !evict ||
		    m_Entries.contains(key))
			return {RHIErrorCode::InvalidDescription, 0, "Residency/Track"};
		if (m_Entries.size() >= m_MaxEntries || bytes > m_Limit || m_Used > m_Limit - bytes) {
			++m_Rejected;
			return {RHIErrorCode::OutOfMemory, 0, "Residency/PayloadBudget"};
		}
		if (m_Age == UINT64_MAX)
			return {RHIErrorCode::OutOfMemory, 0, "Residency/AgeExhausted"};
		Entry entry;
		entry.bytes = bytes;
		entry.age = ++m_Age;
		entry.mayEvict = std::move(mayEvict);
		entry.evict = std::move(evict);
		if (const auto status = entry.uses.Reserve(4); !status.IsSuccess())
			return status;
		m_Entries.emplace(key, std::move(entry));
		m_Used += bytes;
		m_Peak = std::max(m_Peak, m_Used);
		return {};
	} catch (const std::bad_alloc&) {
		return {RHIErrorCode::OutOfMemory, 0, "Residency/TrackAllocation"};
	}

	bool ResourceResidencyManager::Touch(
	    ResourceResidencyKey key, std::span<const RHICompletionPoint> points) noexcept {
		if (!Owner())
			return false;
		const auto found = m_Entries.find(key);
		if (found == m_Entries.end() || found->second.ownerReleased)
			return false;
		auto& entry = found->second;
		if (m_Age == UINT64_MAX) {
			entry.poisoned = true;
			return false;
		}
		entry.age = ++m_Age;
		for (const auto point : points)
			if (!entry.uses.TryInclude(point)) {
				entry.poisoned = true;
				return false;
			}
		return true;
	}

	RHIStatus ResourceResidencyManager::MakeRoom(size_t requested, const CompletionQuery& query) try {
		if (!Owner() || !query)
			return {RHIErrorCode::InvalidDescription, 0, "Residency/OwnerOrQuery"};
		if (requested > m_Limit) {
			++m_Rejected;
			return {RHIErrorCode::OutOfMemory, 0, "Residency/RequestTooLarge"};
		}
		std::vector<ResourceResidencyKey> order;
		order.reserve(m_Entries.size());
		for (const auto& [key, entry] : m_Entries)
			if (requested != 0 || entry.ownerReleased || entry.evictionRequested)
				order.push_back(key);
		/* 每帧仅清理失效条目时无须对全部缓存排序；真正申请容量时才按LRU选择。 */
		if (requested != 0)
			std::sort(order.begin(), order.end(),
			    [&](const auto& a, const auto& b) { return m_Entries.at(a).age < m_Entries.at(b).age; });
		for (const auto key : order) {
			const auto found = m_Entries.find(key);
			if (found == m_Entries.end())
				continue;
			auto& entry = found->second;
			if (!entry.ownerReleased && !entry.evictionRequested && m_Used <= m_Limit - requested &&
			    (requested == 0 || m_Entries.size() < m_MaxEntries))
				continue;
			if (entry.poisoned || (!entry.ownerReleased && !entry.mayEvict()))
				continue;
			bool complete = true;
			for (const auto point : entry.uses.GetPoints()) {
				bool done = false;
				const auto status = query(point, done);
				if (!status.IsSuccess())
					return status;
				if (!done) {
					complete = false;
					break;
				}
			}
			if (!complete)
				continue;
			auto evict = entry.ownerReleased ? std::function<void()>{} : std::move(entry.evict);
			const auto bytes = entry.bytes;
			m_Entries.erase(found);
			m_Used -= bytes;
			/* 回调可能再次调用缓存的 Forget，因此先移除当前条目并停止使用其引用。 */
			if (evict)
				evict();
		}
		if (m_Used > m_Limit - requested || (requested != 0 && m_Entries.size() >= m_MaxEntries)) {
			++m_Rejected;
			return {RHIErrorCode::OutOfMemory, 0, "Residency/AllCandidatesPinnedOrInFlight"};
		}
		return {};
	} catch (const std::bad_alloc&) {
		return {RHIErrorCode::OutOfMemory, 0, "Residency/LruAllocation"};
	}

	void ResourceResidencyManager::RequestEviction(ResourceResidencyKey key) noexcept {
		if (!Owner())
			return;
		if (const auto found = m_Entries.find(key); found != m_Entries.end())
			found->second.evictionRequested = true;
	}

	void ResourceResidencyManager::Forget(ResourceResidencyKey key) noexcept {
		if (!Owner())
			return;
		if (const auto found = m_Entries.find(key); found != m_Entries.end())
			found->second.ownerReleased = true;
	}

	void ResourceResidencyManager::ClearAfterDeviceStopped() noexcept {
		if (!Owner())
			std::terminate();
		m_Entries.clear();
		m_Used = 0;
	}
} /* 命名空间 Catalpa */
