#include "catpch.h"
#include "Renderer/DebugDraw.h"

namespace Catalpa {
    RHIStatus DebugDraw::AddLine(const DebugLine& line)
    {
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(line.start[axis]) || !std::isfinite(line.end[axis]))
                return { .code = RHIErrorCode::InvalidDescription, .operation = "DebugDraw/line position" };
        for (int channel = 0; channel < 4; ++channel)
            if (!std::isfinite(line.color[channel]) || line.color[channel] < 0
                || (channel == 3 && line.color[channel] > 1))
                return { .code = RHIErrorCode::InvalidDescription, .operation = "DebugDraw/linear color" };
        if (m_Vertices.size() > 65534)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "DebugDraw/vertex budget" };
        m_Vertices.reserve(m_Vertices.size() + 2);
        const std::array<float, 4> color{ line.color.x, line.color.y, line.color.z, line.color.w };
        m_Vertices.push_back({ { line.start.x, line.start.y, line.start.z }, color });
        m_Vertices.push_back({ { line.end.x, line.end.y, line.end.z }, color });
        return {};
    }

    RHIStatus DebugDraw::AddBounds(const Aabb& bounds, const Math::Vec4& color)
    {
        if (!ValidateBounds(bounds).IsSuccess() || bounds.empty)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "DebugDraw/bounds" };
        DebugDraw candidate = *this;
        std::array<Math::Vec3, 8> corners;
        for (uint32_t index = 0; index < 8; ++index)
            corners[index] = { (index & 1) ? bounds.maximum.x : bounds.minimum.x,
                (index & 2) ? bounds.maximum.y : bounds.minimum.y, (index & 4) ? bounds.maximum.z : bounds.minimum.z };
        for (uint32_t index = 0; index < 8; ++index)
            for (uint32_t axis = 0; axis < 3; ++axis)
                if ((index & (1u << axis)) == 0)
                    if (const auto status = candidate.AddLine({ corners[index], corners[index | (1u << axis)], color }); !status.IsSuccess()) return status;
        m_Vertices.swap(candidate.m_Vertices);
        return {};
    }
}
