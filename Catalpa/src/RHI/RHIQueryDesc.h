#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"

#include <cstdint>
#include <span>

namespace Catalpa {

	/* 当前明确提供时间戳查询；结果可用不等于包含它的整批提交已完成。 */
	struct RHIQueryDesc {
		uint32_t count = 2;
	};
	struct RHIQueryResult {
		uint64_t value = 0;
		bool available = false;
	};

	class CATALPA_API RHIQueryPool {
	public:
		virtual ~RHIQueryPool() = default;
		virtual DeviceId GetDeviceId() const noexcept = 0;
		virtual uint32_t GetCount() const noexcept = 0;
		virtual uint32_t GetTimestampValidBits() const noexcept = 0;
		virtual float GetTimestampPeriodNanoseconds() const noexcept = 0;
		/**
		 * @brief 查询连续槽位的时间戳结果。
		 *
		 * @param[in] first 第一个查询槽位的索引。
		 * @param[in] results 保存查询值及可用标记的目标区间。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus GetResults(uint32_t first, std::span<RHIQueryResult> results) = 0;
	};
}
