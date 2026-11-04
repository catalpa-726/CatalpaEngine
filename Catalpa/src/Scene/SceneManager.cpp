#include "catpch.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Log/Log.h"

#include <atomic>
#include <new>

namespace Catalpa {
    namespace {
        // 生命周期身份不回绕；用尽后新Manager无法发布SceneHandle。
        uint64_t AllocateManagerIdentity() noexcept
        {
            static std::atomic<uint64_t> next{ 1 };
            uint64_t value = next.load(std::memory_order_relaxed);
            while (value != 0) {
                const uint64_t following = value == std::numeric_limits<uint64_t>::max() ? 0 : value + 1;
                if (next.compare_exchange_weak(value, following, std::memory_order_relaxed)) return value;
            }
            return 0;
        }
    }

    SceneManager::SceneManager() : m_Identity(AllocateManagerIdentity()), m_OwnerThread(std::this_thread::get_id()) {}

    SceneManager::~SceneManager()
    {
        // 销毁管理器必须发生在创建线程且所有借用已结束，不能在回调里强行释放World。
        if (const WorldStatus status = Clear(); !status.IsSuccess()) {
            if (Log::GetCoreLogger()) CAT_CORE_CRITICAL("SceneManager destruction is unsafe: {0}", status.message);
            std::abort();
        }
    }

    WorldStatus SceneManager::CanMutate() const noexcept
    {
        if (m_OwnerThread != std::this_thread::get_id()) return { WorldError::Busy, "SceneManager belongs to another thread" };
        if (m_Updating) return { WorldError::Busy, "SceneManager is updating its active scene" };
        for (const auto& slot : m_Slots)
            if (slot.scene && slot.scene->GetWorld().IsAccessActive()) return { WorldError::Busy, "A managed scene is currently borrowed or committing" };
        return {};
    }

    WorldStatus SceneManager::Validate(SceneHandle handle) const noexcept
    {
        if (m_OwnerThread != std::this_thread::get_id()) return { WorldError::Busy, "SceneManager belongs to another thread" };
        if (!handle.HasValue()) return { WorldError::InvalidArgument, "SceneHandle is invalid" };
        if (handle.owner != m_Identity) return { WorldError::ForeignWorld, "SceneHandle belongs to another manager" };
        if (handle.slot.index >= m_Slots.size()) return { WorldError::InvalidArgument, "Scene slot is out of range" };
        const Slot& slot = m_Slots[handle.slot.index];
        if (!slot.scene || slot.retired || slot.generation != handle.slot.generation) return { WorldError::InvalidArgument, "SceneHandle is stale or unloaded" };
        return {};
    }

    WorldStatus SceneManager::CreateScene(std::string name, SceneHandle& output)
    {
        if (const WorldStatus status = CanMutate(); !status.IsSuccess()) return status;
        if (m_Identity == 0) return { WorldError::CapacityExceeded, "SceneManager identities exhausted" };
        constexpr uint32_t invalid = GenerationalHandle<SceneTag>::InvalidIndex;
        uint32_t index = invalid;
        for (size_t i = 0; i < m_Slots.size(); ++i) {
            if (!m_Slots[i].scene && !m_Slots[i].retired) { index = static_cast<uint32_t>(i); break; }
        }
        if (index == invalid && m_Slots.size() >= invalid) return { WorldError::CapacityExceeded, "Scene slot capacity exhausted" };
        try {
            // 场景构造和槽位分配均完成后发布句柄；失败保留调用方output和已有场景。
            auto scene = std::make_unique<Scene>(std::move(name));
            if (index == invalid) {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }
            Slot& slot = m_Slots[index];
            slot.scene = std::move(scene);
            output = { m_Identity, { index, slot.generation } };
            return {};
        } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot allocate scene or manager slot" }; }
    }

    Scene* SceneManager::GetScene(SceneHandle handle) noexcept
    {
        return Validate(handle).IsSuccess() ? m_Slots[handle.slot.index].scene.get() : nullptr;
    }

    const Scene* SceneManager::GetScene(SceneHandle handle) const noexcept
    {
        return Validate(handle).IsSuccess() ? m_Slots[handle.slot.index].scene.get() : nullptr;
    }

    void SceneManager::ReleaseSlot(uint32_t index) noexcept
    {
        Slot& slot = m_Slots[index];
        slot.scene.reset();
        if (slot.generation == std::numeric_limits<uint32_t>::max()) slot.retired = true;
        else ++slot.generation;
    }

    WorldStatus SceneManager::Unload(SceneHandle handle)
    {
        if (const WorldStatus status = CanMutate(); !status.IsSuccess()) return status;
        if (const WorldStatus status = Validate(handle); !status.IsSuccess()) return status;
        if (m_Active == handle) m_Active = {};
        ReleaseSlot(handle.slot.index);
        return {};
    }

    WorldStatus SceneManager::SetActive(SceneHandle handle)
    {
        if (const WorldStatus status = CanMutate(); !status.IsSuccess()) return status;
        if (const WorldStatus status = Validate(handle); !status.IsSuccess()) return status;
        m_Active = handle;
        return {};
    }

    Scene* SceneManager::GetActive() noexcept { return GetScene(m_Active); }
    const Scene* SceneManager::GetActive() const noexcept { return GetScene(m_Active); }

    WorldStatus SceneManager::UpdateActive()
    {
        if (const WorldStatus status = CanMutate(); !status.IsSuccess()) return status;
        if (!m_Active.HasValue()) return {};
        if (const WorldStatus status = Validate(m_Active); !status.IsSuccess()) return status;
        m_Updating = true;
        struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{ m_Updating };
        Scene& scene = *m_Slots[m_Active.slot.index].scene;
        // 出错时保留逐命令诊断，不继续消费尚未重新发布的派生矩阵。
        if (const WorldStatus status = scene.CommitPending(); !status.IsSuccess()) return status;
        return TransformSystem::Update(scene);
    }

    WorldStatus SceneManager::Clear() noexcept
    {
        if (const WorldStatus status = CanMutate(); !status.IsSuccess()) return status;
        m_Active = {};
        for (size_t index = 0; index < m_Slots.size(); ++index)
            if (m_Slots[index].scene) ReleaseSlot(static_cast<uint32_t>(index));
        return {};
    }
}
