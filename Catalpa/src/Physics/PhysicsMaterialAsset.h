#pragma once
#include "Physics/PhysicsTypes.h"
namespace Catalpa {
	struct PhysicsMaterialAsset {
		float friction = 0.5f, restitution = 0;
	};
	/**
	 * @brief 校验物理材质。
	 *
	 * @param[in] value 待检查摩擦与恢复参数的物理材质。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidatePhysicsMaterial(const PhysicsMaterialAsset&, std::string& error);
	/**
	 * @brief 读取物理材质。
	 *
	 * @param[in] text 描述物理材质参数的 JSON 文本。
	 * @param[out] output 接收 PhysicsMaterialAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadPhysicsMaterial(std::string_view, PhysicsMaterialAsset&, std::string& error);
	/**
	 * @brief 写入物理材质。
	 *
	 * @param[in] value 将写入文档的物理材质资产。
	 * @param[out] output 成功时接收记录接触材质参数的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WritePhysicsMaterial(const PhysicsMaterialAsset&, std::string&, std::string& error);
} /* 命名空间 Catalpa */
