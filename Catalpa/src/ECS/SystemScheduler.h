#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/System.h"

namespace Catalpa {

	/* 创建线程上的确定性 CPU 调度器。Scene 拥有本对象，外部服务保持借用。 */
	class CATALPA_API SystemScheduler {
	public:
		SystemScheduler();
		~SystemScheduler() noexcept;
		SystemScheduler(const SystemScheduler&) = delete;
		SystemScheduler& operator=(const SystemScheduler&) = delete;

		/**
		 * @brief 将系统声明加入调度器，使已有编排需要重新编译。
		 *
		 * @param[in] system 要注册的系统、阶段及访问声明。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus Add(SystemDesc system);
		/**
		 * @brief 按标识移除系统，使已有编排需要重新编译。
		 *
		 * @param[in] id 待移除的系统标识。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus Remove(std::string_view id);
		/**
		 * @brief 在调度器空闲时清除系统注册及执行计划。
		 * 非运行时整体撤销注册与计划，用于恢复含环/失效依赖的配置；不分配或提交场景命令。
		 *
		 * @return 允许修改并成功清除时返回正常状态，否则返回当前访问限制。
		 */
		[[nodiscard]] WorldStatus Clear() noexcept;
		/**
		 * @brief 从系统阶段和显式依赖建立有向图，检查读写冲突后生成可执行顺序。
		 *
		 * @return 图无环、冲突均有顺序且成功发布执行计划时返回正常状态，否则返回依赖、访问或分配错误。
		 */
		[[nodiscard]] WorldStatus Compile();
		/**
		 * @brief 运行阶段。
		 *
		 * @param[in] phase 本次只执行登记在该阶段的系统。
		 * @param[in] context 当前操作的上下文。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus RunPhase(SystemPhase phase, SystemContext& context);
		/**
		 * @brief 向目标调度器复制玩法系统，用于构造独立的运行场景。
		 *
		 * @param[out] target 接收玩法系统的调度器。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus CloneGameplayTo(Scene& target) const;
		/**
		 * @brief 检查是否具有系统。
		 *
		 * @param[in] id 调度器中待查找的系统标识。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		[[nodiscard]] bool HasSystem(std::string_view id) const noexcept;
		[[nodiscard]] bool IsRunning() const noexcept { return m_Running; }
		[[nodiscard]] std::string_view GetLastDiagnostic() const noexcept { return m_LastDiagnostic; }

	private:
		[[nodiscard]] WorldStatus CanMutate() noexcept;
		[[nodiscard]] WorldStatus Fail(
		    WorldError code, const char* message, std::string_view first = {}, std::string_view second = {}) noexcept;

		std::thread::id m_OwnerThread;
		std::vector<SystemDesc> m_Systems;
		std::vector<std::size_t> m_Order;
		/* 借出的诊断只在下一次调度操作之前有效。 */
		std::string m_LastDiagnostic;
		bool m_Compiled = false;
		bool m_Running = false;
	};

}
