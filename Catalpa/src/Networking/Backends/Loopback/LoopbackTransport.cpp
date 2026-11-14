#include "catpch.h"

#include "Networking/Backends/Loopback/LoopbackTransport.h"
namespace Catalpa {
	namespace {
		struct Mailboxes {
			std::mutex mutex;
			std::array<std::deque<NetworkMessage>, 2> messages;
			std::array<size_t, 2> bytes{};
			std::array<bool, 2> closed{};
			NetworkLimits limits;
		};
		class LoopbackTransport final : public INetworkTransport {
		public:
			LoopbackTransport(std::shared_ptr<Mailboxes> pair, size_t side) : m_Pair(std::move(pair)), m_Side(side) {}

			~LoopbackTransport() override { Close(); }

			bool Send(std::span<const std::byte> bytes, std::string& error) override {
				std::lock_guard lock(m_Pair->mutex);
				const auto peer = 1 - m_Side;
				if (m_Pair->closed[m_Side] || m_Pair->closed[peer]) {
					error = "Loopback connection is closed";
					return false;
				}
				const auto& limits = m_Pair->limits;
				if (bytes.empty() || bytes.size() > limits.maxMessageBytes ||
				    m_Pair->messages[peer].size() >= limits.maxMessages ||
				    bytes.size() > limits.maxQueuedBytes - m_Pair->bytes[peer]) {
					error = "Loopback message or queue budget exceeded";
					return false;
				}
				try {
					NetworkMessage message{{bytes.begin(), bytes.end()}};
					m_Pair->messages[peer].push_back(std::move(message));
					m_Pair->bytes[peer] += bytes.size();
					error.clear();
					return true;
				} catch (const std::exception& e) {
					error = e.what();
					return false;
				}
			}

			/**
			 * @brief 从环回邮箱取出下一条消息。
			 * 空队列成功返回 available=false；断连和畸形输入单独报错。
			 *
			 * @param[out] out 接收取出的消息字节。
			 * @param[out] available 本次是否取到一条消息。
			 * @param[out] error 失败时接收具体诊断。
			 *
			 * @return 接收操作正常时为 true，空队列同样成功并令 available 为 false；传输出错时为 false。
			 */
			bool Receive(NetworkMessage& out, bool& available, std::string& error) override {
				std::lock_guard lock(m_Pair->mutex);
				available = false;
				if (m_Pair->closed[m_Side]) {
					error = "Loopback endpoint is closed";
					return false;
				}
				auto& queue = m_Pair->messages[m_Side];
				if (queue.empty()) {
					if (m_Pair->closed[1 - m_Side]) {
						error = "Loopback peer disconnected";
						return false;
					}
					error.clear();
					return true;
				}
				const auto size = queue.front().payload.size();
				out = std::move(queue.front());
				queue.pop_front();
				m_Pair->bytes[m_Side] -= size;
				available = true;
				error.clear();
				return true;
			}

			void Close() noexcept override {
				std::lock_guard lock(m_Pair->mutex);
				m_Pair->closed[m_Side] = true;
				m_Pair->messages[m_Side].clear();
				m_Pair->bytes[m_Side] = 0;
			}

		private:
			std::shared_ptr<Mailboxes> m_Pair;
			size_t m_Side;
		};
	} /* 命名空间 */
	bool CreateLoopbackPair(std::unique_ptr<INetworkTransport>& first, std::unique_ptr<INetworkTransport>& second,
	    std::string& error, NetworkLimits limits) {
		if (&first == &second || !limits.maxMessageBytes || limits.maxMessageBytes > 1024 * 1024 ||
		    limits.maxQueuedBytes < limits.maxMessageBytes || limits.maxQueuedBytes > 64 * 1024 * 1024 ||
		    !limits.maxMessages || limits.maxMessages > 4096) {
			error = "Invalid loopback budgets";
			return false;
		}
		try {
			auto pair = std::make_shared<Mailboxes>();
			pair->limits = limits;
			auto a = std::make_unique<LoopbackTransport>(pair, 0);
			auto b = std::make_unique<LoopbackTransport>(pair, 1);
			first = std::move(a);
			second = std::move(b);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
