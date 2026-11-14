#pragma once

namespace Catalpa {

	enum class RHIErrorCode {
		None,
		Unsupported,
		InvalidDescription,
		OutOfMemory,
		DeviceLost,
		BackendFailure,
		InvalidState /* 当前状态不允许操作 */
	};

	struct RHIStatus {
		RHIErrorCode code = RHIErrorCode::None;
		int nativeCode = 0; /* 失败状态中，0 表示未提供原生错误码。 */
		const char* operation = ""; /* 只引用字符串字面量。 */

		[[nodiscard]] constexpr bool IsSuccess() const noexcept { return code == RHIErrorCode::None; }
	};

}