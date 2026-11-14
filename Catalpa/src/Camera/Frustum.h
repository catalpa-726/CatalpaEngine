#pragma once

#include "catpch.h"

#include "Camera/CameraLens.h"
#include "Geometry/Bounds.h"

namespace Catalpa {

	/* 内侧满足 n·world+d>=0；double单位法线避免提取后再舍入到float移动裁剪边界。 */
	struct FrustumPlane {
		std::array<double, 4> coefficients{};
		/* 原clip行的绝对值和除以同一法线长度，用于覆盖r3-r2等消去前的float点积误差。 */
		std::array<double, 4> absoluteClipCoefficients{};
		bool active = false;
	};

	/*
	 * 左右/上下指clip轴，flipY后不等同世界方向；Near/Far按镜头语义排列。
	 * 无限远透视只有Far可以inactive，其系数规范为全0；其余面必须有效。
	 */
	enum class FrustumSide : size_t { Left, Right, Bottom, Top, Near, Far, Count };
	struct Frustum {
		std::array<FrustumPlane, static_cast<size_t>(FrustumSide::Count)> planes{};
	};

	/**
	 * @brief 从最终视图投影矩阵提取世界空间视锥，处理反向深度和无限远面。
	 * 列向量 VP 使用 -w ≤ x,y ≤ w 和 0 ≤ z ≤ w，已含 Y 方向与抖动。
	 * 普通深度的近、远面为 r2 和 r3 − r2，反向深度时交换；无限远面须对应恒真的裁剪条件。
	 *
	 * @param[in] viewProjection 将世界空间列向量映射至裁剪空间的最终矩阵。
	 * @param[in] lens 包含投影模式、裁剪距离和深度方向的镜头参数。
	 * @param[out] output 成功时写入世界空间视锥。
	 *
	 * @return 所有裁剪平面提取并验证成功时返回正常状态。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildFrustum(
	    const Math::Mat4& viewProjection, const CameraLens& lens, Frustum& output) noexcept;

	/**
	 * @brief 以各裁剪平面的支撑点检验世界空间包围盒是否与视锥相交。
	 * 逐平面检查 AABB 的最大支撑点，贴面及误差范围内保留。空包围盒成功返回 false，非法输入保留原输出。
	 *
	 * @param[in] frustum 已提取的世界空间裁剪面。
	 * @param[in] bounds 世界空间轴对齐包围盒。
	 * @param[out] intersects 接收给定包围体是否与视锥体相交。
	 *
	 * @return 输入合法并完成测试时返回成功，具体相交结果写入 intersects。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus IntersectsFrustum(
	    const Frustum& frustum, const Aabb& bounds, bool& intersects) noexcept;

}
