#include "catpch.h"
#include "ECS/World.h"
#include "ECS/Detail/WorldComponentType.h"

#include <atomic>
#include <new>
#include <thread>

namespace Catalpa {

    namespace {

        constexpr uint32_t InvalidIndex = GenerationalHandle<EntityTag>::InvalidIndex;

        // 身份耗尽后保持零，不回绕到已经发出的 World 身份。
        uint64_t AllocateWorldIdentity() noexcept {
            static std::atomic<uint64_t> next{ 1 };
            uint64_t value = next.load(std::memory_order_relaxed);
            while (value != 0) {
                const uint64_t following = value == std::numeric_limits<uint64_t>::max() ? 0 : value + 1;
                if (next.compare_exchange_weak(value, following, std::memory_order_relaxed)) return value;
            }
            return 0;
        }

        // 所有组件复制/回调异常只在 ECS 调用边界转换；不把异常视作成功。
        template<typename Operation> WorldStatus InvokeChecked(Operation&& operation) {
            try { return operation(); }
            catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "ECS allocation failed" }; }
            catch (...) { return { WorldError::CommandFailed, "ECS component operation or callback failed" }; }
        }

        struct ReadGuard {
            uint32_t& depth;
            explicit ReadGuard(uint32_t& value) : depth(value) { ++depth; }
            ~ReadGuard() { --depth; }
        };

        struct MutationGuard {
            bool& active;
            explicit MutationGuard(bool& value) : active(value) { active = true; }
            ~MutationGuard() { active = false; }
        };

    }

    struct World::Impl {
        struct Slot {
            entt::entity native = entt::null;
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            bool alive = false;
        };

        entt::registry registry;
        std::vector<Slot> slots;
        uint64_t identity = AllocateWorldIdentity();
        std::thread::id ownerThread = std::this_thread::get_id();
        size_t aliveCount = 0;
        uint32_t freeHead = InvalidIndex;
        uint32_t readDepth = 0;
        bool mutating = false;
        bool applyingCommands = false;
    };

    World::World() : m_Impl(std::make_unique<Impl>()) {}
    World::~World() = default;

    uint64_t World::GetIdentity() const noexcept { return m_Impl->identity; }
    size_t World::GetEntityCount() const noexcept { return m_Impl->ownerThread == std::this_thread::get_id() ? m_Impl->aliveCount : 0; }
    bool World::IsAccessActive() const noexcept {
        if (m_Impl->ownerThread != std::this_thread::get_id()) return true;
        return m_Impl->readDepth != 0 || m_Impl->mutating || m_Impl->applyingCommands;
    }
    bool World::IsAlive(EntityId entity) const noexcept { return Validate(entity).IsSuccess(); }

    WorldStatus World::CountComponents(EntityId entity, size_t& count) const {
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        const auto native = m_Impl->slots[entity.index].native;
        size_t result = 0;
        for (const auto& [id, storage] : m_Impl->registry.storage()) {
            (void)id;
            if (storage.contains(native)) ++result;
        }
        count = result;
        return {};
    }

    WorldStatus World::Validate(EntityId entity) const noexcept {
        if (m_Impl->ownerThread != std::this_thread::get_id()) return { WorldError::Busy, "World belongs to another thread" };
        if (m_Impl->mutating) return { WorldError::Busy, "World is changing component storage" };
        if (!entity.HasValue()) return { WorldError::InvalidEntity, "Invalid entity identity" };
        if (entity.world != m_Impl->identity) return { WorldError::ForeignWorld, "Entity belongs to another World" };
        if (entity.index >= m_Impl->slots.size()) return { WorldError::InvalidEntity, "Entity slot is out of range" };
        const auto& slot = m_Impl->slots[entity.index];
        if (!slot.alive || slot.generation != entity.generation || !m_Impl->registry.valid(slot.native)) return { WorldError::InvalidEntity, "Entity is stale or destroyed" };
        return {};
    }

    WorldStatus World::CanMutate() const noexcept {
        if (m_Impl->ownerThread != std::this_thread::get_id()) return { WorldError::Busy, "World belongs to another thread" };
        if (m_Impl->readDepth != 0 || m_Impl->mutating) return { WorldError::Busy, "World is being read or mutated" };
        return {};
    }

    WorldStatus World::Create(EntityId& output) {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (m_Impl->identity == 0) return { WorldError::CapacityExceeded, "World identities exhausted" };
        if (m_Impl->aliveCount >= entt::entt_traits<entt::entity>::entity_mask) return { WorldError::CapacityExceeded, "Native entity capacity exhausted" };
        const bool append = m_Impl->freeHead == InvalidIndex;
        if (append && m_Impl->slots.size() >= InvalidIndex) return { WorldError::CapacityExceeded, "Entity slot capacity exhausted" };

        MutationGuard guard(m_Impl->mutating);
        return InvokeChecked([&]() -> WorldStatus {
            // 分配槽数组先于 native create；之后只有不抛异常的值写入，失败不发布 ID。
            if (append && m_Impl->slots.size() == m_Impl->slots.capacity()) {
                const size_t growth = std::max<size_t>(1, std::min<size_t>(m_Impl->slots.size(), InvalidIndex - m_Impl->slots.size()));
                m_Impl->slots.reserve(m_Impl->slots.size() + growth);
            }
            const entt::entity native = m_Impl->registry.create();
            const uint32_t index = append ? static_cast<uint32_t>(m_Impl->slots.size()) : m_Impl->freeHead;
            if (append) m_Impl->slots.emplace_back();
            auto& slot = m_Impl->slots[index];
            if (!append) m_Impl->freeHead = slot.nextFree;
            slot.native = native;
            slot.nextFree = InvalidIndex;
            slot.alive = true;
            ++m_Impl->aliveCount;
            output = { m_Impl->identity, index, slot.generation };
            return {};
        });
    }

    WorldStatus World::Destroy(EntityId entity) {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        MutationGuard guard(m_Impl->mutating);
        return InvokeChecked([&]() -> WorldStatus {
            auto& slot = m_Impl->slots[entity.index];
            m_Impl->registry.destroy(slot.native);
            slot.native = entt::null;
            slot.alive = false;
            --m_Impl->aliveCount;
            // 代次即将回绕的槽永久退役，不能让旧句柄重新匹配。
            if (slot.generation == std::numeric_limits<uint32_t>::max()) slot.nextFree = InvalidIndex;
            else {
                ++slot.generation;
                slot.nextFree = m_Impl->freeHead;
                m_Impl->freeHead = entity.index;
            }
            return {};
        });
    }

    WorldStatus World::ForEach(const std::function<WorldStatus(EntityId)>& visitor) const {
        if (!visitor) return { WorldError::InvalidArgument, "Entity visitor is empty" };
        if (m_Impl->ownerThread != std::this_thread::get_id() || m_Impl->mutating || m_Impl->readDepth == std::numeric_limits<uint32_t>::max()) return { WorldError::Busy, "World cannot enter a read scope" };
        ReadGuard guard(m_Impl->readDepth);
        return InvokeChecked([&]() -> WorldStatus {
            for (size_t index = 0; index < m_Impl->slots.size(); ++index) {
                const auto& slot = m_Impl->slots[index];
                if (!slot.alive) continue;
                if (const auto status = visitor({ m_Impl->identity, static_cast<uint32_t>(index), slot.generation }); !status.IsSuccess()) return status;
            }
            return {};
        });
    }

    bool World::HasComponent(EntityId entity, const ComponentType& type) const noexcept {
        if (!Validate(entity).IsSuccess()) return false;
        return type.find(m_Impl->registry, m_Impl->slots[entity.index].native) != nullptr;
    }

    WorldStatus World::ReadComponent(EntityId entity, const ComponentType& type, void* context, WorldStatus (*reader)(const void*, void*)) const {
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        if (m_Impl->readDepth == std::numeric_limits<uint32_t>::max()) return { WorldError::Busy, "World read scope capacity exhausted" };
        ReadGuard guard(m_Impl->readDepth);
        return InvokeChecked([&]() -> WorldStatus {
            const void* value = type.find(m_Impl->registry, m_Impl->slots[entity.index].native);
            if (!value) return { WorldError::MissingComponent, "Component does not exist" };
            return reader(value, context);
        });
    }

    WorldStatus World::AddComponent(EntityId entity, const ComponentType& type, const void* value) {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        MutationGuard guard(m_Impl->mutating);
        return InvokeChecked([&] { return type.add(m_Impl->registry, m_Impl->slots[entity.index].native, value); });
    }

    WorldStatus World::SetComponent(EntityId entity, const ComponentType& type, const void* value) {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        MutationGuard guard(m_Impl->mutating);
        return InvokeChecked([&] { return type.set(m_Impl->registry, m_Impl->slots[entity.index].native, value); });
    }

    WorldStatus World::RemoveComponent(EntityId entity, const ComponentType& type) {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (const auto status = Validate(entity); !status.IsSuccess()) return status;
        MutationGuard guard(m_Impl->mutating);
        return InvokeChecked([&] { return type.remove(m_Impl->registry, m_Impl->slots[entity.index].native); });
    }

    WorldStatus World::BeginCommands() noexcept {
        if (const auto status = CanMutate(); !status.IsSuccess()) return status;
        if (m_Impl->applyingCommands) return { WorldError::Busy, "World is already applying commands" };
        m_Impl->applyingCommands = true;
        return {};
    }

    void World::EndCommands() noexcept { m_Impl->applyingCommands = false; }

}
