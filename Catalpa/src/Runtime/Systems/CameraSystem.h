#pragma once

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "ECS/World.h"

namespace Catalpa {

	class Scene;

	/* 将相机组件及已经更新的刚性世界变换转换为完整相机矩阵值。 */
	class CATALPA_API CameraSystem {
	public:
		/**
		 * @brief 读取场景中的相机实体并生成给定视口的相机数据。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] camera 从中提取镜头和变换的相机实体。
		 * @param[in] viewport 构建相机投影时使用的目标视口尺寸。
		 * @param[out] output 接收相机矩阵结果。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] static WorldStatus Build(
		    const Scene& scene, EntityId camera, const CameraViewport& viewport, CameraMatrices& output);
	};

}
