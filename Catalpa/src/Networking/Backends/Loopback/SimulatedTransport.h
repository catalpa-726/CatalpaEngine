#pragma once
#include "Networking/Backends/Loopback/LoopbackTransport.h"
namespace Catalpa {
    // reorder 给奇数包额外两 tick 抖动，并倒序交付同时到期的包，真正覆盖跨包乱序。
    struct LoopbackFaults {
        uint32_t delayTicks = 0, dropEvery = 0, duplicateEvery = 0;
        bool reverseDuePackets = false;
    };
    // 调试故障按明确 tick 推进，不等待墙钟、不启用真实网络、不隐式重传传输层数据。
    CATALPA_API bool CreateSimulatedLoopbackPair(std::unique_ptr<INetworkTransport>&,
                                                 std::unique_ptr<INetworkTransport>&, LoopbackFaults,
                                                 std::string& error, NetworkLimits = {});
} // namespace Catalpa
