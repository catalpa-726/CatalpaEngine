#pragma once
#include "Networking/ReplicationSchema.h"
#include "Networking/Backends/Loopback/SimulatedTransport.h"
namespace Catalpa {
    struct LoopbackSettings {
        uint32_t tickRate = 60;
        ReplicationLimits limits;
        LoopbackFaults faults;
    };
    CATALPA_API bool ReadLoopbackSettings(std::string_view, LoopbackSettings&, std::string& error);
    CATALPA_API bool WriteLoopbackSettings(const LoopbackSettings&, std::string&, std::string& error);
} // namespace Catalpa
