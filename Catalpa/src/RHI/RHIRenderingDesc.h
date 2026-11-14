#pragma once

#include "catpch.h"

#include "RHIHandle.h"
#include "RHITypes.h"

/* 描述颜色附件、加载／保存方式、清除颜色和渲染区域 */
namespace Catalpa {

	enum class RHILoadOp { Load, Clear, DontCare };
	enum class RHIStoreOp { Store, DontCare };

	struct RHIColorAttachment {
		TextureViewHandle view{};
		RHILoadOp loadOp = RHILoadOp::Clear;
		RHIStoreOp storeOp = RHIStoreOp::Store;
		std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
	};

	/* 深度只读段必须Load/Store，且绑定的管线不可写深度；layout仍由图声明。 */
	struct RHIDepthAttachment {
		TextureViewHandle view{};
		RHILoadOp loadOp = RHILoadOp::Clear;
		RHIStoreOp storeOp = RHIStoreOp::Store;
		float clearDepth = 1.0f;
		bool readOnly = false;
	};

	/* 只借用附件描述数组，调用者须保持数组有效至录制调用结束。 */
	struct RHIRenderingDesc {
		std::span<const RHIColorAttachment> colorAttachments{};
		Rect2D renderArea{};
		uint32_t sampleCount = 1;
		std::optional<RHIDepthAttachment> depthAttachment;
	};

}