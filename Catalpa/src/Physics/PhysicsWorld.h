#pragma once
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
namespace Catalpa {
	/* SDK 的物理世界由单个线程持有。所有句柄都记录世界代次，公共接口按值返回。 */
	class CATALPA_API PhysicsWorld final {
	public:
		/**
		 * @brief 初始化 PhysicsWorld。
		 */
		PhysicsWorld();
		~PhysicsWorld();
		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;
		/**
		 * @brief 根据配置建立 Jolt 物理世界、容量和碰撞筛选器。
		 *
		 * @param[in] config 重力、对象上限及过滤设置。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Initialize(const PhysicsWorldConfig&, std::string& error);
		/**
		 * @brief 在所有者线程销毁物理世界与其 Jolt 资源。
		 */
		void Shutdown() noexcept;
		/**
		 * @brief 创建刚体。
		 *
		 * @param[in] desc 包含初始姿态和运动设置的刚体描述。
		 * @param[in] shape 用于生成刚体碰撞几何的形状资产。
		 * @param[in] material 用于设定刚体接触属性的物理材质。
		 * @param[out] out 接收 PhysicsBodyHandle 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool CreateBody(const PhysicsBodyDesc&, const CollisionShapeAsset&, const PhysicsMaterialAsset&,
		    PhysicsBodyHandle&, std::string& error);
		/**
		 * @brief 销毁刚体。
		 *
		 * @param[in] handle 目标对象的 PhysicsBodyHandle。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool DestroyBody(PhysicsBodyHandle, std::string& error);
		/**
		 * @brief 读取刚体。
		 *
		 * @param[in] handle 目标对象的 PhysicsBodyHandle。
		 * @param[out] output 接收 PhysicsBodyState 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ReadBody(PhysicsBodyHandle, PhysicsBodyState&, std::string& error) const;
		/**
		 * @brief 设置姿态。
		 *
		 * @param[in] handle 目标对象的 PhysicsBodyHandle。
		 * @param[in] pose 写入目标刚体的世界空间位置和朝向。
		 * @param[in] teleport 是否立即迁移到目标位置。
		 * @param[in] dt 本次模拟或采样步长，单位为秒。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetPose(PhysicsBodyHandle, const PhysicsPose&, bool teleport, double dt, std::string& error);
		/**
		 * @brief 推进一次物理固定步并关联当前仿真 tick。
		 *
		 * @param[in] seconds 固定步长（秒）。
		 * @param[in] tick 递增的仿真 tick 编号。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Step(double seconds, uint64_t tick, std::string& error);
		/**
		 * @brief 沿指定射线查询物理世界中的命中。
		 *
		 * @param[in] origin 起始位置。
		 * @param[in] delta 从射线起点到查询终点的位移，长度为最大检测距离。
		 * @param[out] out 接收 PhysicsQueryHit 结果。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] layerMask 指定哪些碰撞层可参与射线查询的位掩码。
		 * @param[in] ignore 忽略目标。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Raycast(Math::Vec3 origin, Math::Vec3 delta, PhysicsQueryHit&, std::string& error,
		    uint16_t layerMask = 0xffff, PhysicsBodyHandle ignore = {}) const;
		/**
		 * @brief 使形状从起始姿态沿位移扫掠并返回命中结果。
		 *
		 * @param[in] asset 用于扫掠的碰撞形状。
		 * @param[in] pose 查询起始姿态。
		 * @param[in] delta 形状从起始姿态到目标位置的位移向量。
		 * @param[out] out 接收命中信息。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] layerMask 允许参与查询的碰撞层位掩码。
		 * @param[in] ignore 从候选中排除的刚体句柄。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Sweep(const CollisionShapeAsset&, const PhysicsPose&, Math::Vec3 delta, PhysicsQueryHit&,
		    std::string& error, uint16_t layerMask = 0xffff, PhysicsBodyHandle ignore = {}) const;
		/**
		 * @brief 按给定形状和姿态查找重叠刚体。
		 *
		 * @param[in] asset 重叠检查所用的碰撞形状。
		 * @param[in] pose 查询形状的世界姿态。
		 * @param[out] output 接收重叠结果。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] layerMask 接受的碰撞层位掩码。
		 * @param[in] maxHits 本次查询允许收集的最大命中数。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Overlap(const CollisionShapeAsset&, const PhysicsPose&, std::vector<PhysicsQueryHit>&, std::string& error,
		    uint16_t layerMask = 0xffff, size_t maxHits = 256) const;
		/**
		 * @brief 创建角色。
		 *
		 * @param[in] settings 新角色的碰撞和移动参数。
		 * @param[in] pose 角色创建时的世界空间位置及朝向。
		 * @param[out] output 接收 PhysicsBodyHandle 结果。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] replacing 替换状态。
		 * @param[in] owner 标识持有该对象的上下文。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool CreateCharacter(const CharacterSettings&, const PhysicsPose&, PhysicsBodyHandle&, std::string& error,
		    PhysicsBodyHandle replacing = {}, EntityReference owner = {});
		/**
		 * @brief 按期望速度和时间步更新虚拟角色，并返回移动结果。
		 *
		 * @param[in] handle 要移动的角色句柄。
		 * @param[in] desiredVelocity 世界空间期望速度。
		 * @param[in] seconds 本次模拟或采样步长，单位为秒。
		 * @param[out] output 接收角色移动后的运动信息。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool MoveCharacter(
		    PhysicsBodyHandle, Math::Vec3 desiredVelocity, double seconds, CharacterMotion&, std::string& error);
		/**
		 * @brief 直接设置虚拟角色的位置与朝向。
		 *
		 * @param[in] handle 目标角色句柄。
		 * @param[in] pose 传送后的世界姿态。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool TeleportCharacter(PhysicsBodyHandle, const PhysicsPose&, std::string& error);
		/**
		 * @brief 销毁角色。
		 *
		 * @param[in] handle 目标对象的 PhysicsBodyHandle。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool DestroyCharacter(PhysicsBodyHandle, std::string& error);
		/**
		 * @brief 取出事件。
		 *
		 * @param[out] output 接收 std::vector<PhysicsContactEvent> 结果。
		 */
		void TakeEvents(std::vector<PhysicsContactEvent>&);
		uint64_t DroppedEvents() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
