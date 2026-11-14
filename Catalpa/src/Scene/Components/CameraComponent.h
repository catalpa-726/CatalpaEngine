#pragma once

#include "Camera/CameraLens.h"
#include "ECS/World.h"

namespace Catalpa {

	/*
	 * 镜头选择与稳定视图身份；像素尺寸由消费者提供，不持有窗口或 RenderSurface。
	 * historyRevision 在瞬移/切镜头时由作者明确递增，S03.4 再消费历史。
	 */
	struct CameraComponent {
		CameraLens lens;
		bool enabled = true;
		int32_t priority = 0;
		uint64_t viewId = 1;
		uint64_t historyRevision = 1;
	};
	template <> struct ComponentTraits<CameraComponent> {
		[[nodiscard]] static CATALPA_API const ComponentType& Get();
	};

}
