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
    // 受管payload预算与LRU；不把估算payload字节冒称驱动/进程的总显存。
    class CATALPA_API ResourceResidencyManager final {
      public:
        using CompletionQuery = std::function<RHIStatus(const RHICompletionPoint&, bool&)>;
        explicit ResourceResidencyManager(size_t payloadBudget = 512 * 1024 * 1024,
                                          size_t maxEntries = 8192);
        RHIStatus Track(ResourceResidencyKey, size_t bytes, std::function<bool()> mayEvict,
                        std::function<void()> evict);
        // 预留完成点容量发生在Track；已接受提交后的Touch不分配。
        bool Touch(ResourceResidencyKey, std::span<const RHICompletionPoint>) noexcept;
        RHIStatus MakeRoom(size_t requiredBytes, const CompletionQuery&);
        // 失效版本保留可重用的缓存owner，等外部lease与所有queue使用结束后再淘汰。
        void RequestEviction(ResourceResidencyKey) noexcept;
        // owner已释放仍保留账目，直到MakeRoom核实所有已接受使用的完成点。
        void Forget(ResourceResidencyKey) noexcept;
        // 调用者先停止使用并等待/失设备收束；此接口本身不制造GPU完成证明。
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
} // namespace Catalpa
