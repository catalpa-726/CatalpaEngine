#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIError.h"
#include "Log/Log.h"

/* 集中复用已有的枚举和错误转换逻辑 */
namespace Catalpa {

	/**
	 * @brief 以 Vulkan 的计数、读取两阶段接口枚举元素，数量在读取期间变化则重试。
	 *
	 * @param[out] values 接收枚举元素的缓冲区；成功时长度与实际读到的元素数一致。
	 * @param[in] query 兼容 Vulkan 枚举约定的回调，用空指针查询数量，再用已分配数组接收元素。
	 *
	 * @return 完整读取后返回 VK_SUCCESS；第二阶段返回 VK_INCOMPLETE 时重试，其余失败返回原生结果。
	 */
	template <typename T> VkResult EnumerateVulkan(std::vector<T>& values, auto query) {
		for (;;) {
			uint32_t count = 0;
			if (const VkResult result = query(&count, nullptr); result != VK_SUCCESS)
				return result;

			values.resize(count);
			if (count == 0)
				return VK_SUCCESS;

			const VkResult result = query(&count, values.data());
			if (result == VK_INCOMPLETE)
				continue;
			if (result == VK_SUCCESS)
				values.resize(count);
			return result;
		}
	}

	/**
	 * @brief 将 Vulkan 调用结果转换为 RHI 状态，遇到失败则记录对应操作名和原生错误码。
	 *
	 * @param[in] result 本次 Vulkan 调用返回的原生结果码。
	 * @param[in] operation 失败时写入日志并保存在状态中的操作名；字符数据须在返回状态的使用期间有效。
	 *
	 * @return VK_SUCCESS 对应成功；其余结果保留原生错误码并映射到 RHI 错误类别。
	 */
	inline RHIStatus MakeVulkanStatus(VkResult result, const char* operation) {
		if (result == VK_SUCCESS)
			return {};

		RHIErrorCode code = RHIErrorCode::BackendFailure;

		switch (result) {
		case VK_ERROR_DEVICE_LOST:
			code = RHIErrorCode::DeviceLost;
			break;
		case VK_ERROR_OUT_OF_HOST_MEMORY:
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			code = RHIErrorCode::OutOfMemory;
			break;
		case VK_ERROR_FORMAT_NOT_SUPPORTED:
			code = RHIErrorCode::Unsupported;
			break;
		default:
			break;
		}

		CAT_CORE_ERROR("{0} failed. VkResult: {1}", operation, static_cast<int>(result));
		return {.code = code, .nativeCode = static_cast<int>(result), .operation = operation};
	}

}