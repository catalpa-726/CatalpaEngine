#pragma once
#include "Renderer/RenderView.h"
#include "Scene/Scene.h"
#include "Geometry/Bounds.h"

namespace Catalpa::Editor {
	/**
	 * @brief 从已显示视图的鼠标位置求最近网格表面或地面放置点。
	 * @param[in] scene 只读的作者场景，网格必须具有有效世界变换。
	 * @param[in] view 实际显示帧的投影，沿用 Vulkan Y 和深度约定。
	 * @param[in] u 鼠标在图像中的水平比例，范围 0 到 1。
	 * @param[in] v 鼠标在图像中的垂直比例，范围 0 到 1。
	 * @param[out] position 成功时接收世界坐标，单位为米。
	 * @return 反投影有效且得到有限坐标时为 true。
	 */
	bool FindViewportPlacement(const Scene& scene, const RenderView& view, float u, float v, Math::Vec3& position);

	/**
	 * @brief 汇总选中子树的网格世界包围盒；无网格节点采用其世界原点。
	 * @param[in] scene 含有效世界变换的作者场景。
	 * @param[in] root 选中的根实体。
	 * @param[out] bounds 成功时接收子树包围盒。
	 * @param[out] error 层级或数学无效时接收原因。
	 * @return 完整遍历并得到有效包围盒时为 true。
	 */
	bool SelectionBounds(const Scene& scene, EntityId root, Aabb& bounds, std::string& error);
}
