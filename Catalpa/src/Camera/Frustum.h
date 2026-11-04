#pragma once

#include "catpch.h"
#include "Camera/CameraLens.h"
#include "Geometry/Bounds.h"

namespace Catalpa {

    // 内侧满足 n·world+d>=0；double单位法线避免提取后再舍入到float移动裁剪边界。
    struct FrustumPlane {
        std::array<double, 4> coefficients{};
        // 原clip行的绝对值和除以同一法线长度，用于覆盖r3-r2等消去前的float点积误差。
        std::array<double, 4> absoluteClipCoefficients{};
        bool active = false;
    };

    // 左右/上下指clip轴，flipY后不等同世界方向；Near/Far按镜头语义排列。
    // 无限远透视只有Far可以inactive，其系数规范为全0；其余面必须有效。
    enum class FrustumSide : size_t { Left, Right, Bottom, Top, Near, Far, Count };
    struct Frustum {
        std::array<FrustumPlane, static_cast<size_t>(FrustumSide::Count)> planes{};
    };

    // 从最终列向量VP提取 -w<=x,y<=w、0<=z<=w；不再翻Y或变更jitter。
    // 调用者负责VP与已验证相机的一致性；本函数另验有限/可逆、面退化与lens分支。
    // 普通Z的near=r2、far=r3-r2，reversed-Z交换二者；失败保留output。
    [[nodiscard]] CATALPA_API Math::MathStatus BuildFrustum(const Math::Mat4& viewProjection,
        const CameraLens& lens, Frustum& output) noexcept;

    // 用AABB的最大支撑点保守测试；贴面/误差范围内保留，可产生少量假可见而不误剔除。
    // 验证全部平面和bounds后才分类；非法输入保留intersects，规范空bounds成功返回false。
    [[nodiscard]] CATALPA_API Math::MathStatus IntersectsFrustum(const Frustum& frustum,
        const Aabb& bounds, bool& intersects) noexcept;

}
