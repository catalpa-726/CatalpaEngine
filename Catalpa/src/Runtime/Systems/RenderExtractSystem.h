#pragma once

#include "ECS/World.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

	class Scene;

	/*
	 * 一个输出view的CPU请求；cameraViewId=0沿用优先级选择，否则只在对应相机组中选择。
	 * id是输出身份，允许同一相机服务不同尺寸的view；目标代次由目标拥有者提供，不是GPU句柄。
	 */
	struct RenderViewRequest {
		uint64_t id = 1;
		uint64_t cameraViewId = 0;
		uint64_t targetGeneration = 1;
		CameraViewport viewport;
	};

	/* 派生状态更新后复制到自有 packet，失败不发布半帧，不保留组件指针。 */
	class CATALPA_API RenderExtractSystem {
	public:
		/**
		 * @brief 将场景、相机及可见实例提取为当前帧的渲染数据。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] sequence 标识此次提取结果所属帧的序号。
		 * @param[in] width UI 或渲染目标的宽度，以像素计。
		 * @param[in] height UI 或渲染目标的高度，以像素计。
		 * @param[out] output 接收 RenderFramePacket 结果。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] static WorldStatus Extract(
		    const Scene& scene, uint64_t sequence, uint32_t width, uint32_t height, RenderFramePacket& output);
		[[nodiscard]] static WorldStatus Extract(const Scene& scene, uint64_t sequence,
		    std::span<const RenderViewRequest> requests, RenderFramePacket& output);
	};

}
