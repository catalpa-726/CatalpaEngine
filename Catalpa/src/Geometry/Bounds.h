#pragma once

#include "catpch.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

    // 空包围体有显式标记；没有以无穷值哨兵混入数学或渲染数据。
    struct Aabb {
        Math::Vec3 minimum{ 0.0f };
        Math::Vec3 maximum{ 0.0f };
        bool empty = true;
    };

    struct BoundingSphere {
        Math::Vec3 center{ 0.0f };
        float radius = 0.0f;
        bool empty = true;
    };

    // 均不修改原始点/变换；失败保留数据 output，空输入返回规范空包围体。
    [[nodiscard]] CATALPA_API Math::MathStatus ValidateBounds(const Aabb& bounds) noexcept;
    [[nodiscard]] CATALPA_API Math::MathStatus ComputeBounds(std::span<const Math::Vec3> points, Aabb& output) noexcept;
    [[nodiscard]] CATALPA_API Math::MathStatus MergeBounds(const Aabb& left, const Aabb& right, Aabb& output) noexcept;
    // double 中间值并向外舍入，支持非均匀/负缩放及 affine shear。
    [[nodiscard]] CATALPA_API Math::MathStatus TransformBounds(const Aabb& local, const Math::Mat4& world, Aabb& output) noexcept;
    // 球包围 AABB；中心 float 舍入误差计入半径，不以原始中心半径冒充保守包围。
    [[nodiscard]] CATALPA_API Math::MathStatus ComputeBoundingSphere(const Aabb& bounds, BoundingSphere& output) noexcept;

}
