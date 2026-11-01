#pragma once

#include "Core/Base.h"
#include "RHI/RHIError.h"

namespace Catalpa {

    class Window;
    class RHIDevice;

    class EngineRuntime {
    public:
        EngineRuntime();
        ~EngineRuntime();

        EngineRuntime(const EngineRuntime&) = delete;
        EngineRuntime& operator=(const EngineRuntime&) = delete;

        [[nodiscard]] RHIStatus Init(Window& window);
        void Shutdown();

    private:
        std::unique_ptr<RHIDevice> m_Device;
    };

}