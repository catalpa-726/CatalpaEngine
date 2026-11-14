#pragma once

#include "catpch.h"

#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

	/* 同一密封几何的可见实例组；组和组内实例都按来源首次出现次序保留，不排序指针。 */
	struct VisibleMeshGroup {
		MeshResourceRef mesh;
		std::vector<size_t> instanceIndices;
	};

	struct VisibleSubmesh {
		size_t instanceIndex = 0;
		uint32_t submeshIndex = 0;
		double cameraDepth = 0;
	};

	/* 全部索引均指向来源packet.instances；分组不改变主索引顺序，不复制或归一化实例。 */
	struct VisibilityResult {
		std::vector<size_t> instanceIndices;
		std::vector<VisibleMeshGroup> groups;
		std::vector<VisibleSubmesh> opaque;
		std::vector<VisibleSubmesh> masked;
		std::vector<VisibleSubmesh> transparent; /* 从后向前；相交透明几何不宣称OIT。 */
	};

	/* 只消费RenderScene已验证的自有值快照；不遍历World、不创建GPU对象。 */
	class VisibilitySystem {
	public:
		/**
		 * @brief 额外检查最终视图投影矩阵、镜头及全部包围盒。零尺寸时直接发布空结果，无须构造投影。
		 * 非零尺寸的非法输入或分配失败会保留输出，隐藏和剔除只影响结果，不改动源实例。
		 *
		 * @param[in] view 提供相机与视口信息的视图。
		 * @param[in] instances 本帧候选绘制实例。
		 * @param[out] output 成功时输出可见实例在 instances 中的索引；空视口产生空列表。
		 *
		 * @return 返回视图与场景包围盒的校验及筛选状态；零尺寸视口成功输出空列表。
		 */
		[[nodiscard]] static CATALPA_API Math::MathStatus Build(
		    const RenderView& view, std::span<const RenderMeshInstance> instances, VisibilityResult& output);
	};

}
