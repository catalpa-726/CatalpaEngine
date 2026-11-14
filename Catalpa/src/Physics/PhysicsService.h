#pragma once
#include "Physics/PhysicsWorld.h"
#include "Core/Serialization/Archive.h"
#include "Scene/Components/RigidBodyComponent.h"
#include "Scene/Components/ColliderComponent.h"
#include "Scene/Components/CharacterControllerComponent.h"
namespace Catalpa {
	class Scene;
	class AssetManager;
	using DynamicComponentMap = std::map<std::string, ArchiveValue, std::less<>>;
	/**
	 * @brief 捕获物理组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in,out] values 接收实体刚体、碰撞体和角色控制器的序列化记录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CapturePhysicsComponents(const Scene&, EntityId, DynamicComponentMap&, std::string& error);
	/**
	 * @brief 恢复物理组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] values 从中读取刚体、碰撞体和角色控制器的组件记录。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestorePhysicsComponents(Scene&, EntityId, const DynamicComponentMap&, std::string& error);
	/**
	 * @brief 从场景归档恢复刚体、碰撞体和角色控制器组件。
	 * Scene恢复事务专用：调用者负责整个实体的失败回滚。
	 *
	 * @param[in,out] world 实体及组件所在的 ECS 世界。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] values 从场景快照读取的物理组件字段，按组件类型区分。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 全部声明的物理组件应用成功时为 true，否则为 false，回滚由外层场景事务负责。
	 */
	CATALPA_API bool RestorePhysicsComponents(World&, EntityId, const DynamicComponentMap&, std::string& error);
	/**
	 * @brief 校验物理组件。
	 *
	 * @param[in] values 包含待检查物理组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidatePhysicsComponents(const DynamicComponentMap&, std::string& error);
	/**
	 * @brief 从物理组件作者数据中提取引用的碰撞形状和物理材质标识。
	 *
	 * @param[in] values 以组件名索引的作者归档集合。
	 *
	 * @return 上述遍历或筛选得到的资产标识列表。
	 */
	CATALPA_API std::vector<AssetId> PhysicsComponentDependencies(const DynamicComponentMap&);
	/**
	 * @brief 设置刚体刚体。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] value 要设置到目标实体的刚体运动与物理参数。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus SetRigidBody(Scene&, EntityId, const RigidBodyComponent&);
	/**
	 * @brief 设置碰撞体。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] value 要设置到目标实体的碰撞形状和材质引用。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus SetCollider(Scene&, EntityId, const ColliderComponent&);
	/**
	 * @brief 设置角色控制器。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] value 要设置到目标实体的角色控制器配置。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus SetCharacterController(Scene&, EntityId, const CharacterControllerComponent&);

	/* Runtime 拥有。场景切换会清空旧SDK关联，场景/Jobs本身仍由既有Runtime拥有。 */
	class CATALPA_API PhysicsService final {
	public:
		PhysicsService();
		~PhysicsService();
		/**
		 * @brief 在物理世界创建前解析并应用物理配置。
		 * 世界创建前配置；坏配置不改变既有候选，活动世界不接受原地重配。
		 *
		 * @param[in] source 物理世界的 JSON 配置文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 配置通过校验且可在当前阶段应用时为 true，否则为 false。
		 */
		bool ConfigureFromJson(std::string_view, std::string& error);
		/**
		 * @brief 同步场景和物理后端，执行一步仿真，将动态姿态及接触事件交回场景。
		 *
		 * @param[in,out] scene 提供当前实体与世界变换，接收本步产生的局部变换及组件更新。
		 * @param[in,out] assets 提供碰撞形状和物理材质的固定版本；尚未加载时可发起请求。
		 * @param[in] seconds 本步模拟时长，以秒为单位，须为有限值且介于 0（不含）和 0.1（含）之间。
		 * @param[in] tick 宿主模拟步编号，写入本步收集的接触事件。
		 * @param[out] error 失败时写入场景处理或物理后端的诊断。
		 *
		 * @return 本步完成时为 true；步长非法或场景、资源、后端处理出错时为 false。
		 */
		bool Tick(Scene&, AssetManager&, double seconds, uint64_t tick, std::string& error);
		/**
		 * @brief 确保已准备好世界。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool EnsureWorld(Scene&, std::string& error);
		/**
		 * @brief 清除角色与刚体关联并释放当前场景的物理世界。
		 */
		void Stop() noexcept;
		PhysicsWorld* World() noexcept;
		/**
		 * @brief 取出事件。
		 *
		 * @param[out] out 接收 std::vector<PhysicsContactEvent> 结果。
		 */
		void TakeEvents(std::vector<PhysicsContactEvent>&);
		const std::vector<std::string>& Diagnostics() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
