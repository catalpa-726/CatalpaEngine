#pragma once
#include "Navigation/NavigationWorld.h"
#include "Physics/PhysicsService.h"
#include "Scene/Components/NavigationAgentComponent.h"

namespace Catalpa {
	class JobSystem;
	/**
	 * @brief 捕获导航组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in,out] values 接收实体的导航代理序列化字段。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureNavigationComponents(const Scene&, EntityId, DynamicComponentMap&, std::string&);
	/**
	 * @brief 恢复导航组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] values 从中恢复导航代理组件的序列化字段。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreNavigationComponents(Scene&, EntityId, const DynamicComponentMap&, std::string&);
	/**
	 * @brief 恢复导航组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] values 从中恢复导航代理组件的序列化字段。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreNavigationComponents(World&, EntityId, const DynamicComponentMap&, std::string&);
	/**
	 * @brief 校验导航组件。
	 *
	 * @param[in] values 包含待检查导航代理记录的组件字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateNavigationComponents(const DynamicComponentMap&, std::string&);
	/**
	 * @brief 从作者组件集合提取导航代理引用的网格资产标识。
	 *
	 * @param[in] values 按组件名索引的作者归档集合。
	 *
	 * @return 上述遍历或筛选得到的资产标识列表。
	 */
	CATALPA_API std::vector<AssetId> NavigationComponentDependencies(const DynamicComponentMap&);
	/**
	 * @brief 设置导航代理。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] value 要写入目标实体的导航代理设置。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus SetNavigationAgent(Scene&, EntityId, const NavigationAgentComponent&);

	/* 借用Runtime唯一Jobs。Worker只读取不可变tile并输出自有值路径，不访问World。 */
	class CATALPA_API NavigationService final {
	public:
		NavigationService();
		~NavigationService();
		/**
		 * @brief 同步场景导航代理并推进本次固定步的查询与移动。
		 *
		 * @param[in] scene 代理所属的场景。
		 * @param[in] assets 用于解析导航网格版本的资产管理器。
		 * @param[in] jobs 执行后台查询的作业系统。
		 * @param[in] seconds 本次模拟或采样步长，单位为秒。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Tick(Scene&, AssetManager&, JobSystem&, double seconds, std::string& error);
		/**
		 * @brief 取消并等待导航作业，释放场景关联和查询资源。
		 */
		void Stop() noexcept;
		const std::vector<std::string>& Diagnostics() const noexcept;
		/**
		 * @brief 返回路径。
		 *
		 * @param[in] id 用于定位目标的持久实体标识。
		 * @param[out] output 接收 NavigationPath 结果。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		bool GetPath(PersistentEntityId, NavigationPath&) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
