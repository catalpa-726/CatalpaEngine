#pragma once

#include "Core/Math/MathTypes.h"
#include "Geometry/Bounds.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    struct DebugLine {
        Math::Vec3 start{}, end{};
        Math::Vec4 color{ 1.0f };
    };
    struct DebugVertex { std::array<float, 3> position{}; std::array<float, 4> color{}; };
    static_assert(sizeof(DebugVertex) == 28);

    // 自有CPU调试几何；没有设备或World指针。Frame清理由提交端显式决定。
    class CATALPA_API DebugDraw {
    public:
        [[nodiscard]] RHIStatus AddLine(const DebugLine&);
        [[nodiscard]] RHIStatus AddBounds(const Aabb&, const Math::Vec4& color);
        void Clear() noexcept { m_Vertices.clear(); }
        [[nodiscard]] std::span<const DebugVertex> GetVertices() const noexcept { return m_Vertices; }
    private:
        std::vector<DebugVertex> m_Vertices;
    };
}
