#pragma once
#include "Assets/AssetHandle.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
    inline constexpr size_t MaximumSkeletonJoints = 256;
    struct SkeletonJoint {
        StableId id;
        std::string name;
        int32_t parent = -1;
        Math::TransformTRS bindLocal;
        Math::Mat4 inverseBind{1};
    };
    // 列向量，model=parent*local。inverseBind 是 Skeleton bind-model 空间的逆，非 GPU 原生布局。
    struct SkeletonAsset {
        std::vector<SkeletonJoint> joints;
    };
    CATALPA_API bool ValidateSkeleton(const SkeletonAsset&, std::string& error);
    CATALPA_API bool BuildModelPose(const SkeletonAsset&, std::span<const Math::TransformTRS>,
                                    std::vector<Math::Mat4>&, std::string& error);
} // namespace Catalpa
