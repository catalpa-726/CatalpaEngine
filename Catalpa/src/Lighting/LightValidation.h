#pragma once

#include "Core/Base.h"
#include "Lighting/LightParameters.h"
#include "Lighting/EnvironmentSettings.h"

#include <string>

namespace Catalpa {

	/**
	 * @brief 从世界变换的局部负 Z 轴提取光源单位方向。
	 * 从世界变换的局部 -Z 轴派生单位方向；退化或非有限时保留 output。
	 *
	 * @param[in] world 将灯光局部坐标映射到世界坐标的变换矩阵。
	 * @param[out] output 成功时写入局部负 Z 轴对应的世界空间单位方向。
	 *
	 * @return 变换方向有限且可归一化时为 true，否则为 false。
	 */
	[[nodiscard]] bool TryGetWorldLightDirection(const Math::Mat4& world, Math::Vec3& output) noexcept;
	/**
	 * @brief 校验光源参数。
	 *
	 * @param[in] light 光源参数。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] CATALPA_API bool ValidateLightParameters(const LightParameters&, std::string& error);
	/**
	 * @brief 校验环境设置。
	 *
	 * @param[in] settings 待核对环境光照参数的设置。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] CATALPA_API bool ValidateEnvironmentSettings(const EnvironmentSettings&, std::string& error);

}
