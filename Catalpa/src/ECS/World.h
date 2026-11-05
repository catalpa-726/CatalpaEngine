#pragma once

#include "Core/Base.h"
#include "ECS/EntityId.h"

#include <functional>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace Catalpa {

    enum class WorldError { None, InvalidArgument, InvalidEntity, ForeignWorld, Busy, AlreadyExists, MissingComponent, CapacityExceeded, AllocationFailed, CommandFailed, InvalidPresentationView };

    struct WorldStatus {
        WorldError code = WorldError::None;
        const char* message = "";
        [[nodiscard]] bool IsSuccess() const noexcept { return code == WorldError::None; }
    };

    // 描述符的定义和 EnTT 适配只存在于 ECS/Detail；组件头仅声明 Get 的导出特化。
    struct ComponentType;
    template<typename T> struct ComponentTraits;
    class EntityCommandBuffer;

    // 单线程组件所有者。只复制值或在 Read 回调内借用，不向应用暴露 registry/迭代器。
    class CATALPA_API World {
    public:
        World();
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;
        World(World&&) = delete;
        World& operator=(World&&) = delete;

        [[nodiscard]] uint64_t GetIdentity() const noexcept;
        [[nodiscard]] size_t GetEntityCount() const noexcept;
        // 快照必须确认该实体的所有组件都有恢复策略。
        [[nodiscard]] WorldStatus CountComponents(EntityId entity, size_t& count) const;
        // 所有者卸载前查询：读回调、组件变更或命令提交期间不可销毁 World。
        [[nodiscard]] bool IsAccessActive() const noexcept;
        [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;
        [[nodiscard]] WorldStatus Create(EntityId& output);
        [[nodiscard]] WorldStatus Destroy(EntityId entity);

        // 只访问当前实体身份；回调期间同 World 的增删/写入及命令提交返回 Busy。
        [[nodiscard]] WorldStatus ForEach(const std::function<WorldStatus(EntityId)>& visitor) const;

        template<typename T> [[nodiscard]] bool Has(EntityId entity) const noexcept { return HasComponent(entity, ComponentTraits<T>::Get()); }

        // 输出仅在完整复制成功后替换。类型必须支持不抛异常的移动赋值。
        template<typename T> [[nodiscard]] WorldStatus Get(EntityId entity, T& output) const {
            static_assert(std::is_copy_constructible_v<T> && std::is_nothrow_move_assignable_v<T>);
            return Read<T>(entity, [&output](const T& value) {
                T copy(value);
                output = std::move(copy);
                return WorldStatus{};
            });
        }

        // reader 返回 WorldStatus；所借用的引用只在回调期间有效，不可缓存或修改。
        template<typename T, typename Reader> [[nodiscard]] WorldStatus Read(EntityId entity, Reader&& reader) const {
            auto invoke = [&reader](const void* value) -> WorldStatus { return reader(*static_cast<const T*>(value)); };
            return ReadComponent(entity, ComponentTraits<T>::Get(), &invoke,
                [](const void* value, void* callback) { return (*static_cast<decltype(invoke)*>(callback))(value); });
        }

        template<typename T> [[nodiscard]] WorldStatus Add(EntityId entity, const T& value) { return AddComponent(entity, ComponentTraits<T>::Get(), &value); }
        template<typename T> [[nodiscard]] WorldStatus Set(EntityId entity, const T& value) { return SetComponent(entity, ComponentTraits<T>::Get(), &value); }
        template<typename T> [[nodiscard]] WorldStatus Remove(EntityId entity) { return RemoveComponent(entity, ComponentTraits<T>::Get()); }

    private:
        friend class EntityCommandBuffer;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        [[nodiscard]] WorldStatus Validate(EntityId entity) const noexcept;
        [[nodiscard]] WorldStatus CanMutate() const noexcept;
        [[nodiscard]] bool HasComponent(EntityId entity, const ComponentType& type) const noexcept;
        [[nodiscard]] WorldStatus ReadComponent(EntityId entity, const ComponentType& type, void* context, WorldStatus (*reader)(const void*, void*)) const;
        [[nodiscard]] WorldStatus AddComponent(EntityId entity, const ComponentType& type, const void* value);
        [[nodiscard]] WorldStatus SetComponent(EntityId entity, const ComponentType& type, const void* value);
        [[nodiscard]] WorldStatus RemoveComponent(EntityId entity, const ComponentType& type);
        [[nodiscard]] WorldStatus BeginCommands() noexcept;
        void EndCommands() noexcept;
    };

}
