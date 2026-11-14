#include "catpch.h"
#include "VulkanBackend.h"

#include "VulkanLoader.h"
#include "VulkanDevice.h"
#include "Log/Log.h"

namespace Catalpa {

	VulkanBackend::~VulkanBackend() {
		Shutdown();
	}

	RHIStatus VulkanBackend::Init(Window& window, const RHIDiagnosticsOptions& diagnostics) {
		if (m_Started) {
			CAT_CORE_ASSERT(false, "Vulkan backend already started!");
			return {.code = RHIErrorCode::InvalidState, .operation = "VulkanBackend::Init"};
		}

		std::vector<const char*> extensions;

		if (!VulkanSurface::GetRequiredExtensions(extensions))
			return {.code = RHIErrorCode::BackendFailure, .operation = "VulkanSurface::GetRequiredExtensions"};

		m_Started = true;

		RHIStatus status = m_Instance.Init(extensions, diagnostics);

		if (status.IsSuccess() && !m_Surface.Init(m_Instance.GetHandle(), window))
			status = {.code = RHIErrorCode::BackendFailure, .operation = "VulkanSurface::Init"};

		if (status.IsSuccess() &&
		    !m_PhysicalDevice.Init(
		        m_Instance.GetHandle(), m_Surface.GetHandle(), m_Instance.IsSurfaceMaintenanceEnabled()))
			status = {.code = RHIErrorCode::BackendFailure, .operation = "VulkanPhysicalDevice::Init"};

		if (!status.IsSuccess()) {
			Shutdown();
			return status;
		}

		CAT_CORE_INFO("Vulkan backend initialized.");
		return {};
	}

	void VulkanBackend::Shutdown() {
		if (!m_Started)
			return;

		/* GPU 等待由 Runtime 协调；VulkanDevice 在释放本后端前回收设备子资源并销毁逻辑设备。 */
		m_PhysicalDevice = VulkanPhysicalDevice{};
		m_Surface.Shutdown();
		m_Instance.Shutdown();
		VulkanLoader::Shutdown();

		m_Started = false;
	}

	std::unique_ptr<RHIDevice> CreateVulkanDevice(Window& window, const RHIBackendDesc& desc, RHIStatus& status) {
		status = {};

		auto backend = std::make_unique<VulkanBackend>();
		status = backend->Init(window, desc.diagnostics);
		if (!status.IsSuccess())
			return nullptr;

		auto device = std::make_unique<VulkanDevice>();

		if (!device->Init(std::move(backend))) {
			status = {.code = RHIErrorCode::BackendFailure, .operation = "VulkanDevice::Init"};
			return nullptr;
		}

		return device;
	}

}