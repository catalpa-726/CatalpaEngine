#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/System.h"

namespace Catalpa {
	class Scene;

	/* 一个客户端固定步系统的注册令牌；不拥有 Scene/World，也不保存组件或场景指针。 */
	class CATALPA_API GameplayModule {
	public:
		GameplayModule() = default;
		GameplayModule(const GameplayModule&) = delete;
		GameplayModule& operator=(const GameplayModule&) = delete;

		/**
		 * @brief 在场景中注册玩法阶段系统并编译检查执行计划。
		 * 注册一个 Gameplay 阶段系统并编译验证，失败撤销本次注册。
		 *
		 * @param[in,out] scene 拥有目标系统调度器的场景。
		 * @param[in] system 待安装的 Gameplay 阶段系统描述。
		 *
		 * @return 系统安装并通过计划校验时返回成功，否则返回注册、编译或回滚状态。
		 */
		[[nodiscard]] WorldStatus Install(Scene& scene, SystemDesc system);
		/**
		 * @brief 从最初绑定的场景中卸载玩法系统。
		 * 拒绝跨场景或仍被依赖的注销，失败保留注册令牌。
		 *
		 * @param[in,out] scene 安装时绑定的同一场景。
		 *
		 * @return 系统已移除或无须移除时返回成功，场景不符或调度器拒绝时返回错误。
		 */
		[[nodiscard]] WorldStatus Uninstall(Scene& scene);
		[[nodiscard]] bool IsInstalled() const noexcept { return m_WorldIdentity != 0; }
		/**
		 * @brief 清除已卸载场景留下的模块注册令牌。
		 * 场景已经卸载时，只清除本地令牌；场景所有的回调已随调度器销毁。
		 */
		void ForgetUnloadedScene() noexcept;

	private:
		uint64_t m_WorldIdentity = 0;
		std::string m_SystemId;
	};
}
