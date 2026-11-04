#include "catpch.h"
#include "ECS/EntityCommandBuffer.h"

namespace Catalpa {

    WorldStatus EntityCommandBuffer::QueueCreate(Initializer initializer) {
        if (m_Applying) return { WorldError::Busy, "Command buffer is applying" };
        try { m_Commands.push_back({ true, std::move(initializer), {} }); }
        catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot queue entity creation" }; }
        return {};
    }

    WorldStatus EntityCommandBuffer::QueueDestroy(EntityId entity) {
        return Enqueue([entity](World& world) { return world.Destroy(entity); });
    }

    WorldStatus EntityCommandBuffer::Enqueue(Action action) {
        if (m_Applying) return { WorldError::Busy, "Command buffer is applying" };
        if (!action) return { WorldError::InvalidArgument, "Command action is empty" };
        try { m_Commands.push_back({ false, {}, std::move(action) }); }
        catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot queue entity command" }; }
        return {};
    }

    EntityCommandApplyResult EntityCommandBuffer::Apply(World& world) {
        EntityCommandApplyResult result;
        if (m_Applying) {
            result.status = { WorldError::Busy, "Command buffer is already applying" };
            return result;
        }
        // 分配结果先于执行命令；分配失败时整队列保留，World 不变。
        try { result.commands.reserve(m_Commands.size()); }
        catch (const std::bad_alloc&) {
            result.status = { WorldError::AllocationFailed, "Cannot allocate command results" };
            return result;
        }
        if (const auto status = world.BeginCommands(); !status.IsSuccess()) {
            result.status = status;
            return result;
        }

        m_Applying = true;
        for (auto& command : m_Commands) {
            EntityCommandResult commandResult{};
            EntityId pending{};
            try {
                if (command.create) {
                    commandResult.status = world.Create(pending);
                    if (commandResult.status.IsSuccess() && command.initializer) commandResult.status = command.initializer(world, pending);
                    if (commandResult.status.IsSuccess() && !world.IsAlive(pending)) commandResult.status = { WorldError::InvalidEntity, "Initializer destroyed its new entity" };
                } else commandResult.status = command.action(world);
            } catch (const std::bad_alloc&) { commandResult.status = { WorldError::AllocationFailed, "Command allocation failed" }; }
            catch (...) { commandResult.status = { WorldError::CommandFailed, "Command callback failed" }; }

            if (command.create) {
                if (commandResult.status.IsSuccess()) commandResult.created = pending;
                else if (world.IsAlive(pending)) {
                    // 已完成的初始组件同实体一起销毁，失败创建不发布结果。
                    const WorldStatus rollback = world.Destroy(pending);
                    if (!rollback.IsSuccess()) commandResult.status = rollback;
                }
            }
            if (result.status.IsSuccess() && !commandResult.status.IsSuccess()) result.status = commandResult.status;
            result.commands.push_back(commandResult);
        }
        m_Commands.clear();
        m_Applying = false;
        world.EndCommands();
        return result;
    }

    WorldStatus EntityCommandBuffer::Clear() noexcept {
        if (m_Applying) return { WorldError::Busy, "Cannot clear an applying command buffer" };
        m_Commands.clear();
        return {};
    }

}
