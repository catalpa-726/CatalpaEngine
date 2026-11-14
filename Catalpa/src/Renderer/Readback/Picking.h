#pragma once

#include "Renderer/RenderView.h"

namespace Catalpa {

	/* 像素坐标相对已显示的离屏图像，原点左上；不使用桌面/逻辑窗口坐标。 */
	struct PickingRequest {
		uint64_t requestId = 0;
		uint64_t surfaceId = 0;
		uint64_t sceneIdentity = 0;
		uint64_t frameSequence = 0;
		uint64_t targetGeneration = 0;
		uint32_t x = 0;
		uint32_t y = 0;
	};

	enum class PickingResultStatus { Ready, Cancelled, Stale, Failed };

	/* 无命中时 hit 无效；Ready 只表示读回完成，编辑器仍须检查活动文档与实体代次。 */
	struct PickingResult {
		uint64_t ticket = 0;
		PickingRequest request;
		PickingResultStatus status = PickingResultStatus::Failed;
		RenderInstanceId hit;
	};

}
