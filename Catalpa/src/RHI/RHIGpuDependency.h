#pragma once

#include "RHITypes.h"

/* 标识本次获取对应的 GPU 等待与通知，复用后端已有信号量 */
namespace Catalpa {

	enum class RHIGpuDependencyKind { AcquireReady, RenderFinished };

	/*
	 * 当前用于 WSI 的二进制依赖；每次获取只能消费一次。
	 * 当前提交统一采用 AllCommands，后续再扩展细粒度阶段。
	 */
	struct RHIGpuDependency {
		RHIFrameToken source{};
		RHIGpuDependencyKind kind = RHIGpuDependencyKind::AcquireReady;

		[[nodiscard]] constexpr bool HasValue() const noexcept { return source.HasValue(); }
		constexpr bool operator==(const RHIGpuDependency&) const = default;
	};

}