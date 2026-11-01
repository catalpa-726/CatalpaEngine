#include "catpch.h"
#include "EngineRuntime.h"
#include "RHI/RHIBackendFactory.h"
#include "Platform/Vulkan/VulkanBackend.h"
#include "Log/Log.h"

namespace Catalpa {

    EngineRuntime::EngineRuntime() = default;

    EngineRuntime::~EngineRuntime() { Shutdown(); }

    RHIStatus EngineRuntime::Init(Window& window)
    {
        CAT_CORE_ASSERT(!m_Device, "Engine runtime already initialized!");

        RHIBackendFactory factory;
        factory.Register(GraphicsBackend::Vulkan, CreateVulkanDevice);

        RHIStatus status;
        m_Device = factory.Create(GraphicsBackend::Vulkan, window, status);
        return status;
    }

    void EngineRuntime::Shutdown()
    {
        // 当前尚无 GPU 提交；加入提交功能时，在释放设备前完成 GPU 收束。
        m_Device.reset();
    }

}