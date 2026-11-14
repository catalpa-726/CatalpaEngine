#pragma once

#include "RHI/RHIQueryDesc.h"
#include "VulkanHeaders.h"

namespace Catalpa {
	class VulkanDevice;

	/* 唯一的原生池所有者。命令列表共享引用，直至帧槽能够安全重置。 */
	class VulkanQueryPool final : public RHIQueryPool {
	public:
		explicit VulkanQueryPool(VulkanDevice& device) : m_Device(device) {}
		~VulkanQueryPool() override;
		/**
		 * @brief 按描述创建本设备的时间戳查询池。
		 *
		 * @param[in] desc 查询类型和查询槽位数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init(const RHIQueryDesc& desc);
		DeviceId GetDeviceId() const noexcept override;
		uint32_t GetCount() const noexcept override { return m_Count; }
		uint32_t GetTimestampValidBits() const noexcept override { return m_ValidBits; }
		float GetTimestampPeriodNanoseconds() const noexcept override { return m_Period; }
		/**
		 * @brief 不带 WAIT；调用方分别检查每项 available 和自己的帧/提交身份。
		 *
		 * @param[in] first 第一个查询槽位的索引。
		 * @param[out] results 保存查询值及可用标记的目标区间。
		 *
		 * @return 返回查询池读取状态；每项时间戳是否就绪由 results 中的可用标志表示。
		 */
		[[nodiscard]] RHIStatus GetResults(uint32_t first, std::span<RHIQueryResult> results) override;
		VkQueryPool GetHandle() const noexcept { return m_Handle; }

	private:
		VulkanDevice& m_Device;
		VkQueryPool m_Handle = VK_NULL_HANDLE;
		uint32_t m_Count = 0;
		uint32_t m_ValidBits = 0;
		float m_Period = 0.0f;
	};
}
