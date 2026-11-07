#pragma once
#include "RHITypes.h"
namespace Catalpa {
    enum class RHIQueueClass { Graphics, Compute, Transfer };
    struct RHIQueueInfo {
        QueueId id;
        uint32_t family = 0;
        bool graphics = false, compute = false, transfer = false;
    };
    enum class RHIOwnershipPhase { None, Release, Acquire };
    struct RHIQueueTransfer {
        QueueId source, destination;
        RHIOwnershipPhase phase = RHIOwnershipPhase::None;
    };
} // namespace Catalpa
