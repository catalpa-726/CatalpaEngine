#include "catpch.h"
#include "Networking/NetworkSession.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
    NetworkSession::NetworkSession(std::unique_ptr<INetworkTransport> transport, uint64_t id)
        : m_Transport(std::move(transport)), m_SessionId(id) {
        if (!m_Transport || !id)
            throw std::invalid_argument("Network session requires transport and nonzero generation");
    }
    NetworkSession::~NetworkSession() {
        Close();
    }
    bool NetworkSession::AdvanceTick(uint64_t tick, std::string& error) {
        if (!m_Transport) {
            error = "Network session is closed";
            return false;
        }
        return m_Transport->AdvanceTick(tick, error);
    }
    void NetworkSession::Close() noexcept {
        if (m_Transport)
            m_Transport->Close();
        m_Transport.reset();
    }
    bool NetworkSession::Send(uint32_t type, std::span<const std::byte> bytes, std::string& error) {
        if (!m_Transport || !type || bytes.size() > 64 * 1024 - 32 ||
            m_Sent == std::numeric_limits<uint64_t>::max()) {
            error = "Invalid message, closed session or exhausted sequence";
            return false;
        }
        BinaryWriter writer(64 * 1024);
        if (!writer.U32(0x4341544e) || !writer.U32(1) || !writer.U64(m_SessionId) ||
            !writer.U64(m_Sent + 1) || !writer.U32(type) || !writer.U32(uint32_t(bytes.size())) ||
            !writer.Bytes(bytes)) {
            error = writer.Error();
            return false;
        }
        if (!m_Transport->Send(writer.Data(), error))
            return false;
        ++m_Sent;
        return true;
    }
    bool NetworkSession::Receive(uint32_t& type, NetworkMessage& out, bool& available, std::string& error) {
        available = false;
        if (!m_Transport) {
            error = "Network session is closed";
            return false;
        }
        NetworkMessage message;
        bool received = false;
        if (!m_Transport->Receive(message, received, error))
            return false;
        if (!received) {
            error.clear();
            return true;
        }
        BinaryReader reader(message.payload);
        uint32_t magic = 0, version = 0, nextType = 0, count = 0;
        uint64_t id = 0, sequence = 0;
        std::span<const std::byte> payload;
        if (!reader.U32(magic) || magic != 0x4341544e || !reader.U32(version) || version != 1 ||
            !reader.U64(id) || id != m_SessionId || !reader.U64(sequence) || !sequence ||
            !reader.U32(nextType) || !nextType || !reader.U32(count) || count > 64 * 1024 - 32 ||
            !reader.Bytes(count, payload) || !reader.AtEnd()) {
            error = "Stale, duplicate or malformed session message";
            return false;
        }
        if (sequence <= m_Received) {
            const uint64_t distance = m_Received - sequence;
            if (distance >= 64 || (m_ReceivedWindow & (uint64_t(1) << distance))) {
                ++m_Duplicates;
                error.clear();
                return true;
            }
        }
        NetworkMessage candidate;
        try {
            candidate.payload.assign(payload.begin(), payload.end());
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
        out = std::move(candidate);
        type = nextType;
        if (sequence > m_Received) {
            const auto distance = sequence - m_Received;
            m_ReceivedWindow = distance >= 64 ? 1 : (m_ReceivedWindow << distance) | 1;
            m_Received = sequence;
        } else
            m_ReceivedWindow |= uint64_t(1) << (m_Received - sequence);
        available = true;
        error.clear();
        return true;
    }
} // namespace Catalpa
