#pragma once

namespace Catalpa {

	enum class GraphicsBackend { Vulkan };

	/* Synchronization 在基础验证之外追加同步检查。 */
	enum class RHIValidationMode { Disabled, Core, Synchronization };

	struct RHIDiagnosticsOptions {
		RHIValidationMode validation = RHIValidationMode::Disabled;
		bool verboseMessages = false; /* 是否额外接收 Info 和 Verbose 消息。 */
	};

	struct RHIBackendDesc {
		GraphicsBackend backend = GraphicsBackend::Vulkan;
		RHIDiagnosticsOptions diagnostics{};
	};

}