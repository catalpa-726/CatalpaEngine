#pragma once
#include "Core/Base.h"
#include "Networking/INetworkTransport.h"
namespace Catalpa {
	/* sessionId 是连接代次，必须双方约定且重连更换；包序号不写进 World 或持久身份。 */
	class CATALPA_API NetworkSession final {
	public:
		NetworkSession(std::unique_ptr<INetworkTransport>, uint64_t sessionId);
		~NetworkSession();
		/**
		 * @brief 添加协议类型和序号后发送会话消息，并执行大小限制。
		 *
		 * @param[in] type 非零消息类型。
		 * @param[in] bytes 待封装的消息负载。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Send(uint32_t type, std::span<const std::byte>, std::string& error);
		/**
		 * @brief 读取会话消息并校验协议头，分离类型与负载。
		 *
		 * @param[out] type 接收协议消息类型。
		 * @param[out] out 接收消息负载。
		 * @param[in] available 指明本次是否收到了消息。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Receive(uint32_t& type, NetworkMessage&, bool& available, std::string& error);
		/**
		 * @brief 推进模拟步。
		 *
		 * @param[in] tick 当前模拟时刻或步编号。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool AdvanceTick(uint64_t tick, std::string& error);
		uint64_t DroppedDuplicates() const noexcept { return m_Duplicates; }
		/**
		 * @brief 关闭网络会话持有的传输端。
		 */
		void Close() noexcept;

	private:
		std::unique_ptr<INetworkTransport> m_Transport;
		uint64_t m_SessionId = 0, m_Sent = 0, m_Received = 0;
		uint64_t m_ReceivedWindow = 0, m_Duplicates = 0;
	};
} /* 命名空间 Catalpa */
