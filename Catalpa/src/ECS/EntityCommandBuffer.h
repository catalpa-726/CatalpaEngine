#pragma once

#include "ECS/World.h"

#include <functional>
#include <new>
#include <vector>

namespace Catalpa {

    struct EntityCommandResult {
        WorldStatus status;
        EntityId created;
    };

    // 一个结果对应一个 FIFO 命令；独立命令失败不回滚此前已完成的命令。
    struct EntityCommandApplyResult {
        WorldStatus status;
        std::vector<EntityCommandResult> commands;
        [[nodiscard]] bool IsSuccess() const noexcept { return status.IsSuccess(); }
    };

    // 拥有命令值，仅在主线程安全点执行；不持组件地址、registry view 或 iterator。
    class CATALPA_API EntityCommandBuffer {
    public:
        using Action = std::function<WorldStatus(World&)>;
        using Initializer = std::function<WorldStatus(World&, EntityId)>;

        EntityCommandBuffer() = default;
        ~EntityCommandBuffer() = default;
        EntityCommandBuffer(const EntityCommandBuffer&) = delete;
        EntityCommandBuffer& operator=(const EntityCommandBuffer&) = delete;
        EntityCommandBuffer(EntityCommandBuffer&&) = delete;
        EntityCommandBuffer& operator=(EntityCommandBuffer&&) = delete;

        // initializer 只初始化新实体，完成前不得对外发布 ID；失败时销毁实体，created 保持无效。
        // 外部场景索引等状态由调用方在末步发布或自行回滚，不以此承诺任意副作用事务。
        [[nodiscard]] WorldStatus QueueCreate(Initializer initializer = {});
        [[nodiscard]] WorldStatus QueueDestroy(EntityId entity);
        // 自定义领域动作只捕获自有值，事务/层级校验由领域层承担；ECS 仍保证安全点与 FIFO。
        [[nodiscard]] WorldStatus Enqueue(Action action);

        template<typename T> [[nodiscard]] WorldStatus QueueAdd(EntityId entity, const T& value) {
            if (m_Applying) return { WorldError::Busy, "Command buffer is applying" };
            try {
                return Enqueue([entity, copy = T(value)](World& world) { return world.Add<T>(entity, copy); });
            } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot copy command component" }; }
            catch (...) { return { WorldError::CommandFailed, "Cannot copy command component" }; }
        }

        template<typename T> [[nodiscard]] WorldStatus QueueRemove(EntityId entity) { return Enqueue([entity](World& world) { return world.Remove<T>(entity); }); }

        [[nodiscard]] EntityCommandApplyResult Apply(World& world);
        [[nodiscard]] WorldStatus Clear() noexcept;
        [[nodiscard]] size_t Size() const noexcept { return m_Commands.size(); }

    private:
        struct Command {
            bool create = false;
            Initializer initializer;
            Action action;
        };
        std::vector<Command> m_Commands;
        bool m_Applying = false;
    };

}
