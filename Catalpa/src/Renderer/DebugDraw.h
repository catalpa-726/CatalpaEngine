#pragma once

#include "Core/Math/MathTypes.h"
#include "Geometry/Bounds.h"
#include "RHI/RHIError.h"

namespace Catalpa {
	struct DebugLine {
		Math::Vec3 start{}, end{};
		Math::Vec4 color{1.0f};
	};
	struct DebugVertex {
		std::array<float, 3> position{};
		std::array<float, 4> color{};
	};
	static_assert(sizeof(DebugVertex) == 28);

	/* 自有CPU调试几何；没有设备或World指针。Frame清理由提交端显式决定。 */
	class CATALPA_API DebugDraw {
	public:
		/**
		 * @brief 把一条调试线的两个端点及线性颜色追加到本帧顶点列表。
		 *
		 * @param[in] line 含起点、终点和线性 RGBA 的调试线段；分量须有限，颜色非负且透明度不超过 1。
		 *
		 * @return 成功时已追加两个顶点；坐标、颜色或顶点预算不满足条件时返回错误。
		 */
		[[nodiscard]] RHIStatus AddLine(const DebugLine&);
		/**
		 * @brief 把轴对齐包围盒的十二条边作为调试线一次性加入顶点列表。
		 *
		 * @param[in] bounds 用于生成八个角点的非空、有效轴对齐包围盒。
		 * @param[in] color 所有边共用的线性 RGBA 颜色。
		 *
		 * @return 成功时十二条边均已加入；包围盒或任一边无效时保留原顶点列表。
		 */
		[[nodiscard]] RHIStatus AddBounds(const Aabb&, const Math::Vec4& color);
		void Clear() noexcept { m_Vertices.clear(); }
		[[nodiscard]] std::span<const DebugVertex> GetVertices() const noexcept { return m_Vertices; }

	private:
		std::vector<DebugVertex> m_Vertices;
	};
}
