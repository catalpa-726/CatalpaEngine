#pragma once
#include "Networking/NetworkMessage.h"
namespace Catalpa {
    class INetworkTransport {
      public:
        virtual ~INetworkTransport() = default;
        virtual bool Send(std::span<const std::byte>, std::string& error) = 0;
        // 空队列成功返回 available=false；断连和畸形输入单独报错。
        virtual bool Receive(NetworkMessage&, bool& available, std::string& error) = 0;
        virtual bool AdvanceTick(uint64_t, std::string& error) {
            error.clear();
            return true;
        }
        virtual void Close() noexcept = 0;
    };
} // namespace Catalpa
