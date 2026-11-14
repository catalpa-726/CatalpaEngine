#pragma once

#include "catpch.h"

#include "Core/Math/TransformMath.h"

namespace Catalpa {

	/* 空包围体有显式标记；没有以无穷值哨兵混入数学或渲染数据。 */
	struct Aabb {
		Math::Vec3 minimum{0.0f};
		Math::Vec3 maximum{0.0f};
		bool empty = true;
	};

	struct BoundingSphere {
		Math::Vec3 center{0.0f};
		float radius = 0.0f;
		bool empty = true;
	};

	/**
	 * @brief 校验轴对齐包围盒各轴端点的有限性及大小关系。
	 * 均不修改原始点/变换；失败保留数据 output，空输入返回规范空包围体。
	 *
	 * @param[in] bounds 待校验的轴对齐包围盒，包括其空盒表示及最小、最大坐标。
	 *
	 * @return 各端点均有限，且非空包围盒的各轴下界不超过上界时返回成功。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus ValidateBounds(const Aabb& bounds) noexcept;
	/**
	 * @brief 计算包围盒。
	 *
	 * @param[in] points 参与计算的三维采样点。
	 * @param[out] output 接收轴对齐包围盒结果。
	 *
	 * @return 数学运算的状态，用于区分成功和无效或不可表示的输入。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus ComputeBounds(std::span<const Math::Vec3> points, Aabb& output) noexcept;
	/**
	 * @brief 计算包含两个输入包围盒的最小轴对齐包围盒。
	 *
	 * @param[in] left 待合并的轴对齐包围盒，两个输入须处在同一坐标系。
	 * @param[in] right 待合并的轴对齐包围盒，两个输入须处在同一坐标系。
	 * @param[out] output 接收轴对齐包围盒结果。
	 *
	 * @return 数学运算的状态，用于区分成功和无效或不可表示的输入。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus MergeBounds(const Aabb& left, const Aabb& right, Aabb& output) noexcept;
	/**
	 * @brief 将局部包围盒变换到世界空间，并将端点保守地向外舍入。
	 * double 中间值并向外舍入，支持非均匀/负缩放及 affine shear。
	 *
	 * @param[in] local 源局部坐标系中的轴对齐包围盒。
	 * @param[in] world 将局部坐标映射至世界坐标的仿射矩阵。
	 * @param[out] output 成功时写入保守包围变换结果的世界空间轴对齐包围盒。
	 *
	 * @return 得到可表示的世界包围盒时返回成功，否则返回数学错误。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus TransformBounds(
	    const Aabb& local, const Math::Mat4& world, Aabb& output) noexcept;
	/**
	 * @brief 为轴对齐包围盒计算保守包围球。
	 * 球包围 AABB；中心 float 舍入误差计入半径，不以原始中心半径冒充保守包围。
	 *
	 * @param[in] bounds 用来计算球心和半径的轴对齐包围盒。
	 * @param[out] output 成功时接收包围该盒的球体。
	 *
	 * @return 得到可表示的球心及半径时返回成功，否则返回数学错误。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus ComputeBoundingSphere(
	    const Aabb& bounds, BoundingSphere& output) noexcept;

}
