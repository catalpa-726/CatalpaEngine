#pragma once

#include "Core/Math/MathTypes.h"
#include "ECS/World.h"

#include <vector>

namespace Catalpa {

	class Scene;

	/* 迭代拓扑计算完整世界矩阵；全部计算成功后才发布，不使用递归层级遍历。 */
	class CATALPA_API TransformSystem {
	public:
		/**
		 * @brief 计算有改动场景的全部世界变换，暂存组件后再向世界发布。
		 *
		 * @param[in,out] scene 提供待更新的层级和局部变换，成功后写回派生矩阵并清除场景变换的脏标记。
		 *
		 * @return 无需更新或成功写入时返回正常状态；正在提交、层级无效或暂存失败时返回具体错误。
		 */
		[[nodiscard]] static WorldStatus Update(Scene& scene);

	private:
		friend class Scene;
		struct ComputedTransform {
			EntityId entity;
			Math::Mat4 worldMatrix{1.0f};
		};
		[[nodiscard]] static WorldStatus Compute(const Scene& scene, std::vector<ComputedTransform>& output);
	};

}
