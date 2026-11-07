#pragma once
#include "Core/Base.h"
#include "Networking/INetworkTransport.h"
namespace Catalpa {
    // sessionId 是连接代次，必须双方约定且重连更换；包序号不写进 World 或持久身份。
    class CATALPA_API NetworkSession final {
      public:
        NetworkSession(std::unique_ptr<INetworkTransport>, uint64_t sessionId);
        ~NetworkSession();
        bool Send(uint32_t type, std::span<const std::byte>, std::string& error);
        bool Receive(uint32_t& type, NetworkMessage&, bool& available, std::string& error);
        bool AdvanceTick(uint64_t tick, std::string& error);
        uint64_t DroppedDuplicates() const noexcept {
            return m_Duplicates;
        }
        void Close() noexcept;

      private:
        std::unique_ptr<INetworkTransport> m_Transport;
        uint64_t m_SessionId = 0, m_Sent = 0, m_Received = 0;
        uint64_t m_ReceivedWindow = 0, m_Duplicates = 0;
    };
} // namespace Catalpa
