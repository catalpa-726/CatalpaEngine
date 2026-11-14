#include "catpch.h"
#include "VulkanFence.h"

#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

	VulkanFence::VulkanFence(const VulkanDevice& device) : m_Device(device) {}

	VulkanFence::~VulkanFence() {
		Shutdown();
	}

	RHIStatus VulkanFence::Init(bool signaled) {
		CAT_CORE_ASSERT(m_Fence == VK_NULL_HANDLE, "Vulkan fence already exists!");

		const VkFenceCreateInfo createInfo{
		    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u};

		VkFence fence = VK_NULL_HANDLE;
		const RHIStatus status =
		    MakeVulkanStatus(vkCreateFence(m_Device.GetHandle(), &createInfo, nullptr, &fence), "vkCreateFence");
		if (!status.IsSuccess())
			return status;

		m_Fence = fence;
		m_State = signaled ? VulkanFenceState::Signaled : VulkanFenceState::Unsubmitted;
		return {};
	}

	RHIStatus VulkanFence::Wait(uint64_t timeoutNanoseconds, bool& signaled) {
		signaled = m_State == VulkanFenceState::Signaled;
		if (m_State != VulkanFenceState::Pending)
			return {};

		const VkResult result = vkWaitForFences(m_Device.GetHandle(), 1, &m_Fence, VK_TRUE, timeoutNanoseconds);
		if (result == VK_TIMEOUT)
			return {};

		const RHIStatus status = MakeVulkanStatus(result, "vkWaitForFences");

		if (status.IsSuccess()) {
			m_State = VulkanFenceState::Signaled;
			signaled = true;
		}

		return status;
	}

	RHIStatus VulkanFence::Reset() {
		if (m_State == VulkanFenceState::Unsubmitted)
			return {};

		if (m_State == VulkanFenceState::Pending) {
			CAT_CORE_ASSERT(false, "Cannot reset a fence before observing completion!");
			return {.code = RHIErrorCode::InvalidState, .operation = "VulkanFence::Reset"};
		}

		const RHIStatus status = MakeVulkanStatus(vkResetFences(m_Device.GetHandle(), 1, &m_Fence), "vkResetFences");
		if (status.IsSuccess())
			m_State = VulkanFenceState::Unsubmitted;
		return status;
	}

	void VulkanFence::MarkSubmitted() {
		CAT_CORE_ASSERT(
		    m_State == VulkanFenceState::Unsubmitted, "Fence must be unsignaled and available before submission!");
		m_State = VulkanFenceState::Pending;
	}

	void VulkanFence::Shutdown() {
		if (m_Fence != VK_NULL_HANDLE)
			vkDestroyFence(m_Device.GetHandle(), m_Fence, nullptr);
		m_Fence = VK_NULL_HANDLE;
		m_State = VulkanFenceState::Unsubmitted;
	}

}