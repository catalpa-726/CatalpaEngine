#include "catpch.h"

#include "Networking/Backends/Loopback/SimulatedTransport.h"
namespace Catalpa {
	namespace {
		class SimulatedTransport final : public INetworkTransport {
		public:
			SimulatedTransport(std::unique_ptr<INetworkTransport> inner, LoopbackFaults faults, NetworkLimits limits)
			    : m_Inner(std::move(inner)), m_Faults(faults), m_Limits(limits) {}

			/**
			 * @brief 按模拟链路设置把消息排入待交付队列。
			 *
			 * @param[in] bytes 待发送的消息字节。
			 * @param[out] error 失败时接收具体诊断。
			 *
			 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
			 */
			bool Send(std::span<const std::byte> bytes, std::string& error) override try {
				if (m_Closed || bytes.empty() || bytes.size() > m_Limits.maxMessageBytes || m_Serial == UINT64_MAX) {
					error = "Simulated transport closed or message invalid";
					return false;
				}
				const auto serial = m_Serial + 1;
				if (m_Faults.dropEvery && serial % m_Faults.dropEvery == 0) {
					m_Serial = serial;
					error.clear();
					return true;
				}
				const size_t copies = m_Faults.duplicateEvery && serial % m_Faults.duplicateEvery == 0 ? 2 : 1;
				if (copies > m_Limits.maxMessages - m_Pending.size() ||
				    bytes.size() > ((m_Limits.maxQueuedBytes - m_Bytes) / copies) ||
				    m_Tick > UINT64_MAX - uint64_t(m_Faults.delayTicks) - 2) {
					error = "Simulated transport pending budget exhausted";
					return false;
				}
				auto pending = m_Pending;
				for (size_t i = 0; i < copies; ++i)
					pending.push_back(
					    {m_Tick + m_Faults.delayTicks + ((m_Faults.reverseDuePackets && (serial & 1)) ? 2 : 0), serial,
					        {bytes.begin(), bytes.end()}});
				m_Pending.swap(pending);
				m_Bytes += bytes.size() * copies;
				m_Serial = serial;
				error.clear();
				return true;
			} catch (const std::exception& exception) {
				error = exception.what();
				return false;
			}

			bool Receive(NetworkMessage& message, bool& available, std::string& error) override {
				return m_Inner->Receive(message, available, error);
			}

			bool AdvanceTick(uint64_t tick, std::string& error) override {
				if (m_Closed || tick < m_Tick) {
					error = "Simulated transport tick moved backwards";
					return false;
				}
				m_Tick = tick;
				for (;;) {
					auto selected = m_Pending.end();
					for (auto current = m_Pending.begin(); current != m_Pending.end(); ++current)
						if (current->due <= tick &&
						    (selected == m_Pending.end() ||
						        (m_Faults.reverseDuePackets ? current->serial > selected->serial
						                                    : current->serial < selected->serial)))
							selected = current;
					if (selected == m_Pending.end())
						break;
					if (!m_Inner->Send(selected->payload, error))
						return false;
					m_Bytes -= selected->payload.size();
					m_Pending.erase(selected);
				}
				error.clear();
				return true;
			}

			void Close() noexcept override {
				if (m_Closed)
					return;
				m_Closed = true;
				m_Pending.clear();
				m_Bytes = 0;
				m_Inner->Close();
			}

		private:
			struct Pending {
				uint64_t due = 0, serial = 0;
				std::vector<std::byte> payload;
			};
			std::unique_ptr<INetworkTransport> m_Inner;
			LoopbackFaults m_Faults;
			NetworkLimits m_Limits;
			std::vector<Pending> m_Pending;
			uint64_t m_Tick = 0, m_Serial = 0;
			size_t m_Bytes = 0;
			bool m_Closed = false;
		};
	} /* 命名空间 */
	bool CreateSimulatedLoopbackPair(std::unique_ptr<INetworkTransport>& first,
	    std::unique_ptr<INetworkTransport>& second, LoopbackFaults faults, std::string& error,
	    NetworkLimits limits) try {
		if (&first == &second || faults.delayTicks > 32 || faults.dropEvery > 1000000 ||
		    faults.duplicateEvery > 1000000) {
			error = "Loopback fault settings are out of bounds";
			return false;
		}
		std::unique_ptr<INetworkTransport> a, b;
		if (!CreateLoopbackPair(a, b, error, limits))
			return false;
		auto left = std::make_unique<SimulatedTransport>(std::move(a), faults, limits),
		     right = std::make_unique<SimulatedTransport>(std::move(b), faults, limits);
		first = std::move(left);
		second = std::move(right);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa */
