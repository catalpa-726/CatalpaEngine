#pragma once

#include "RHIDevice.h"

namespace Catalpa {

    class Window;

    class RHIBackendFactory {
    public:
        using CreateFunction = std::unique_ptr<RHIDevice>(*)(Window&, RHIStatus&);

        RHIBackendFactory();
        ~RHIBackendFactory();

        RHIBackendFactory(const RHIBackendFactory&) = delete;
        RHIBackendFactory& operator=(const RHIBackendFactory&) = delete;

        void Register(GraphicsBackend backend, CreateFunction create);
        [[nodiscard]] std::unique_ptr<RHIDevice> Create(GraphicsBackend backend, Window& window, RHIStatus& status) const;

    private:
        struct Registry;
        std::unique_ptr<Registry> m_Registry;
    };

}