#pragma once

#include "Core/Math/MathTypes.h"
#include "ECS/World.h"
#include <vector>

namespace Catalpa {

    class Scene;

    // 迭代拓扑计算完整世界矩阵；全部计算成功后才发布，不使用递归层级遍历。
    class CATALPA_API TransformSystem {
    public:
        [[nodiscard]] static WorldStatus Update(Scene& scene);

    private:
        friend class Scene;
        struct ComputedTransform { EntityId entity; Math::Mat4 worldMatrix{ 1.0f }; };
        // 纯快照计算也用于keepWorld重挂接；失败不改Scene及output。
        [[nodiscard]] static WorldStatus Compute(const Scene& scene, std::vector<ComputedTransform>& output);
    };

}
