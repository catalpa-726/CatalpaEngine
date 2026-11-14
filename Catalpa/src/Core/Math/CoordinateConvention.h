#pragma once

#include "MathTypes.h"

namespace Catalpa::Math {

	/* 右手、+Y 向上、-Z 向前；一世界单位为一米，时间秒，角度弧度。 */
	inline const Vec3 Right{1.0f, 0.0f, 0.0f};
	inline const Vec3 Up{0.0f, 1.0f, 0.0f};
	inline const Vec3 Forward{0.0f, 0.0f, -1.0f};
	inline constexpr float Pi = 3.14159265358979323846f;
	inline constexpr float WorldUnitsPerMeter = 1.0f;

	/*
	 * 列优先内存、列向量：local=T*R*S，world=parentWorld*local，pWorld=world*pLocal。
	 * +Y 正旋转 pi/2 将 +X 转至 -Z；GPU packing、Vulkan Y 翻转和 reversed-Z 另由对应阶段定义。
	 */
	inline constexpr bool UsesColumnVectors = true;
	inline constexpr bool UsesColumnMajorStorage = true;
	inline constexpr bool UsesRightHandedCoordinates = true;

}
