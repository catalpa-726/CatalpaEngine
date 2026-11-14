#pragma once
#include "Physics/PhysicsTypes.h"
#include "Assets/AssetHandle.h"
namespace Catalpa {
	struct CollisionShapeAsset {
		CollisionShapeKind kind = CollisionShapeKind::Box;
		/* Box 为半尺寸；Sphere 使用 x 半径；Capsule 使用 x 半径、y 柱体半高，单位米。 */
		Math::Vec3 dimensions{0.5f};
		std::vector<Math::Vec3> vertices;
		std::vector<uint32_t> indices;
	};
	/**
	 * @brief 校验碰撞形状。
	 *
	 * @param[in] value 待检查几何参数的碰撞形状定义。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateCollisionShape(const CollisionShapeAsset&, std::string& error);
	/**
	 * @brief 读取碰撞形状。
	 *
	 * @param[in] text 描述碰撞形状及其几何参数的 JSON 文本。
	 * @param[out] output 接收 CollisionShapeAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadCollisionShape(std::string_view, CollisionShapeAsset&, std::string& error);
	/**
	 * @brief 写入碰撞形状。
	 *
	 * @param[in] value 将写入文档的碰撞形状资产。
	 * @param[out] output 成功时接收记录碰撞形状类型及相应几何参数的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteCollisionShape(const CollisionShapeAsset&, std::string&, std::string& error);
} /* 命名空间 Catalpa */
