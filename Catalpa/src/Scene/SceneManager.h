#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/World.h"
#include "Scene/SceneHandle.h"

namespace Catalpa {

	class Scene;
	class ComponentRegistry;

	/* Runtime唯一拥有的同步场景集合；本步只更新active Scene，后续再接调度/异步资产。 */
	class CATALPA_API SceneManager {
	public:
		SceneManager();
		~SceneManager();
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		/**
		 * @brief 在尚无场景时安装共享只读的组件模式注册表。
		 * 只能在创建线程且尚未创建场景时安装共享的只读模式，以保证已创建场景的规则稳定。
		 *
		 * @param[in] schemas 注册到管理器的共享只读组件模式。
		 *
		 * @return 模式成功安装时返回正常状态；调用线程、模式或场景状态不符合要求时返回错误。
		 */
		[[nodiscard]] WorldStatus SetComponentSchemas(std::shared_ptr<const ComponentRegistry> schemas);

		/**
		 * @brief 创建场景并分配可供后续访问的场景句柄。
		 *
		 * @param[in] name 为新建场景指定的显示名称。
		 * @param[out] output 接收场景句柄结果。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus CreateScene(std::string name, SceneHandle& output);
		/**
		 * @brief 验证场景句柄并借用对应的场景对象。
		 * 指针仅借用到下一次Unload/Clear；业务应长期保存SceneHandle。
		 *
		 * @param[in] handle 目标对象的场景句柄。
		 *
		 * @return 对应场景的指针；无效、过期或外部句柄返回 nullptr。
		 */
		[[nodiscard]] Scene* GetScene(SceneHandle handle) noexcept;
		[[nodiscard]] const Scene* GetScene(SceneHandle handle) const noexcept;
		/**
		 * @brief 卸载场景并使其句柄失效。
		 *
		 * @param[in] handle 要查找或操作的对象句柄。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus Unload(SceneHandle handle);
		/**
		 * @brief 将指定对象设为当前活动对象。
		 *
		 * @param[in] handle 目标对象的场景句柄。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus SetActive(SceneHandle handle);
		[[nodiscard]] Scene* GetActive() noexcept;
		[[nodiscard]] SceneHandle GetActiveHandle() const noexcept { return m_Active; }
		[[nodiscard]] const Scene* GetActive() const noexcept;
		/**
		 * @brief 更新活动。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus UpdateActive();
		/**
		 * @brief 在受保护的活动场景作用域中执行回调。
		 * 在完整 tick/派生/提取作用域保护 active Scene，回调内禁止卸载/切换场景。
		 *
		 * @param[in] operation 在活动场景受保护期间执行的回调，返回操作状态。
		 *
		 * @return 没有活动场景时返回成功。正常执行时透传回调状态；访问失败或捕获异常时返回对应世界错误。
		 */
		[[nodiscard]] WorldStatus WithActive(const std::function<WorldStatus(Scene&)>& operation);
		/**
		 * @brief 在确认所有场景都可卸载后清理整个场景管理器。
		 * 全部场景可卸载后才一次清理；失败不释放任何仍被借用的场景。
		 *
		 * @return 清理完成时返回成功；存在不安全的访问或卸载条件时返回对应错误。
		 */
		[[nodiscard]] WorldStatus Clear() noexcept;

	private:
		struct Slot {
			std::unique_ptr<Scene> scene;
			uint32_t generation = 1;
			bool retired = false;
		};
		[[nodiscard]] WorldStatus CanMutate() const noexcept;
		[[nodiscard]] WorldStatus Validate(SceneHandle handle) const noexcept;
		void ReleaseSlot(uint32_t index) noexcept;
		uint64_t m_Identity = 0;
		std::thread::id m_OwnerThread;
		std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
		std::vector<Slot> m_Slots;
		SceneHandle m_Active;
		bool m_Updating = false;
	};

}
