#pragma once
#include "Core/Base.h"
#include "Networking/INetworkTransport.h"
namespace Catalpa {
    // 两个端点共享有界邮箱；只供进程内验证，不能宣称跨机器、加密或可靠重传已实现。
    CATALPA_API bool CreateLoopbackPair(std::unique_ptr<INetworkTransport>& first,
                                        std::unique_ptr<INetworkTransport>& second, std::string& error,
                                        NetworkLimits limits = {});
} // namespace Catalpa
