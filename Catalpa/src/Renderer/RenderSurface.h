#pragma once

#include "Renderer/RenderView.h"
#include "RHI/RHITypes.h"

namespace Catalpa {

	/* 原生窗口最近一次成功排入呈现队列的像素尺寸和序号。查询 ID 0 指代主交换链。 */
	struct PlatformWindowPresentation {
		Extent2D extent{};
		uint64_t sequence = 0;
	};

	/*
	 * 仅在真实提交后发布的离屏表现快照；token 由 Renderer 注册，不是原生图像地址。
	 * frameSequence/targetGeneration 共同界定显示帧，编辑器不可将旧帧点击套用到新目标。
	 */
	struct RenderSurfaceSnapshot {
		uint64_t surfaceId = 0;
		uint64_t targetGeneration = 0;
		uint64_t sceneIdentity = 0;
		uint64_t frameSequence = 0;
		Extent2D extent{};
		uint64_t textureToken = 0;
		RenderView view;
	};

}
