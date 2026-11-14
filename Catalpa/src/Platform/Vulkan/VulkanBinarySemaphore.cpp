#include "catpch.h"
#include "VulkanBinarySemaphore.h"

#include "VulkanDevice.h"
#include "VulkanUtils.h"

namespace Catalpa {

	VulkanBinarySemaphore::VulkanBinarySemaphore(const VulkanDevice& device) : m_Device(device) {}

	VulkanBinarySemaphore::~VulkanBinarySemaphore() {
		Shutdown();
	}

	RHIStatus VulkanBinarySemaphore::Init() {
		CAT_CORE_ASSERT(m_Semaphore == VK_NULL_HANDLE, "Vulkan semaphore already exists!");

		const VkSemaphoreCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

		VkSemaphore semaphore = VK_NULL_HANDLE;
		const RHIStatus status = MakeVulkanStatus(
		    vkCreateSemaphore(m_Device.GetHandle(), &createInfo, nullptr, &semaphore), "vkCreateSemaphore");
		if (!status.IsSuccess())
			return status;

		m_Semaphore = semaphore;
		return {};
	}

	void VulkanBinarySemaphore::Shutdown() {
		if (m_Semaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(m_Device.GetHandle(), m_Semaphore, nullptr);
		m_Semaphore = VK_NULL_HANDLE;
	}

}