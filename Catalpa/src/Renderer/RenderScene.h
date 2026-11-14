#pragma once

#include "Core/Base.h"
#include "Core/Math/TransformMath.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

	/**
	 * @brief 根据已校验的几何和世界矩阵计算法线及包围盒。拒绝奇异变换，失败时保留原输出。
	 *
	 * @param[in] id 用于标识该绘制实例的非空身份。
	 * @param[in] mesh 已冻结的网格引用，提供几何及局部包围盒。
	 * @param[in] world 从模型局部坐标到世界坐标的可逆仿射变换。
	 * @param[in] color 线性 RGBA 颜色，分量须有限且非负，透明度不得超过 1。
	 * @param[out] output 成功时接收组装后的实例及变换后的世界包围盒。
	 *
	 * @return 返回绘制实例的数学构建状态；矩阵、颜色或包围盒非法时保留原输出。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildRenderMeshInstance(RenderInstanceId id, const MeshResourceRef& mesh,
	    const Math::Mat4& world, const Math::Vec4& color, RenderMeshInstance& output) noexcept;

	/* 渲染方拥有的值快照，不是可独立编辑的第二个 World 或 GPU 资源所有者。 */
	class CATALPA_API RenderScene {
	public:
		/**
		 * @brief 校验并接收严格递增的场景帧快照，拒绝过期帧覆盖当前结果。
		 * sequence 全局严格递增，即使切换来源场景也不接受旧帧覆盖新帧。
		 *
		 * @param[in] packet 所有权转交给渲染场景的候选帧。
		 *
		 * @return 返回帧数据包的校验与接收状态；序号过期或数据无效时保留此前快照。
		 */
		[[nodiscard]] Math::MathStatus Apply(RenderFramePacket packet);
		[[nodiscard]] const RenderFramePacket& GetFrame() const noexcept { return m_Frame; }
		/**
		 * @brief 停止消费者后清快照；生命周期结束时才允许重置序号。
		 */
		void Clear() noexcept;

	private:
		RenderFramePacket m_Frame;
	};

}
