#include "catpch.h"

#include "VulkanAuxiliaryContext.h"

#include "VulkanDevice.h"

namespace Catalpa {
	VulkanAuxiliaryContext::VulkanAuxiliaryContext(VulkanDevice& device, QueueId queue)
	    : m_Device(device), m_Queue(queue), m_Fence(device), m_Pool(device), m_Commands(device) {}

	VulkanAuxiliaryContext::~VulkanAuxiliaryContext() {
		m_Pool.Shutdown();
	}

	RHIStatus VulkanAuxiliaryContext::Init(uint32_t family) {
		if (const auto status = m_Pool.Init(family); !status.IsSuccess())
			return status;
		if (const auto status = m_Commands.Init(m_Pool); !status.IsSuccess())
			return status;
		return m_Fence.Init(true);
	}

	RHIStatus VulkanAuxiliaryContext::PrepareReuse(uint64_t timeout, bool& ready) {
		ready = false;
		if (m_Fence.GetState() == VulkanFenceState::Pending) {
			bool done = false;
			const auto status = m_Fence.Wait(timeout, done);
			if (!status.IsSuccess() || !done)
				return status;
		}
		auto* queue = m_Device.GetQueue(m_Queue);
		if (!queue)
			return {RHIErrorCode::InvalidState, 0, "VulkanAuxiliary/QueueGone"};
		if (m_Fence.GetState() == VulkanFenceState::Signaled)
			static_cast<VulkanQueue*>(queue)->NotifyFenceCompleted(m_Fence);
		if (const auto status = m_Pool.Reset(); !status.IsSuccess())
			return status;
		if (const auto status = m_Commands.OnPoolReset(); !status.IsSuccess())
			return status;
		generation = 0;
		ready = true;
		return {};
	}
} /* 命名空间 Catalpa */
