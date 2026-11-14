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

	/* 一个结果对应一个 FIFO 命令；独立命令失败不回滚此前已完成的命令。 */
	struct EntityCommandApplyResult {
		WorldStatus status;
		std::vector<EntityCommandResult> commands;
		[[nodiscard]] bool IsSuccess() const noexcept { return status.IsSuccess(); }
	};

	/* 命令拥有参数值，仅在主线程安全点执行，不保留组件地址、EnTT 注册表视图或迭代器。 */
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

		/**
		 * @brief 将实体创建与其初始化回调加入结构命令队列。
		 * initializer 只初始化新实体，完成前不得对外发布 ID；失败时销毁实体，created 保持无效。
		 * 外部场景索引等状态由调用方在末步发布或自行回滚，不以此承诺任意副作用事务。
		 *
		 * @param[in] initializer 在实体创建成功时初始化其组件的回调。
		 *
		 * @return 成功排队时返回正常状态。创建和初始化的执行结果由提交阶段提供。
		 */
		[[nodiscard]] WorldStatus QueueCreate(Initializer initializer = {});
		[[nodiscard]] WorldStatus QueueDestroy(EntityId entity);
		/**
		 * @brief 将领域操作移入先进先出的实体命令队列。
		 * 自定义领域动作只捕获自有值，事务/层级校验由领域层承担；ECS 仍保证安全点与 FIFO。
		 *
		 * @param[in] action 待应用到 World 的操作回调。
		 *
		 * @return 操作成功排队时返回正常状态，否则返回队列忙、无效操作或分配失败状态。
		 */
		[[nodiscard]] WorldStatus Enqueue(Action action);

		/**
		 * @brief 将添加组件的操作排入队列。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] value 命令入队时保存、在提交时添加到目标实体的组件值。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		template <typename T> [[nodiscard]] WorldStatus QueueAdd(EntityId entity, const T& value) {
			if (m_Applying)
				return {WorldError::Busy, "Command buffer is applying"};
			try {
				return Enqueue([entity, copy = T(value)](World& world) { return world.Add<T>(entity, copy); });
			} catch (const std::bad_alloc&) {
				return {WorldError::AllocationFailed, "Cannot copy command component"};
			} catch (...) {
				return {WorldError::CommandFailed, "Cannot copy command component"};
			}
		}

		template <typename T> [[nodiscard]] WorldStatus QueueRemove(EntityId entity) {
			return Enqueue([entity](World& world) { return world.Remove<T>(entity); });
		}

		/**
		 * @brief 在世界的结构修改作用域内依次执行排队命令，保留每项的状态和创建结果。
		 *
		 * @param[in,out] world 接收实体创建、组件修改等操作的目标世界，执行期间须可进入命令作用域。
		 *
		 * @return 命令批次结果，包含逐项状态和首个失败；无法开始执行时返回对应错误。
		 */
		[[nodiscard]] EntityCommandApplyResult Apply(World& world);
		/**
		 * @brief 在未应用命令时清空尚未执行的队列。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
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
