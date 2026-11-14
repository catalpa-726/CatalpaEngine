#pragma once
#include "Networking/NetworkMessage.h"
namespace Catalpa {
	class INetworkTransport {
	public:
		virtual ~INetworkTransport() = default;
		/**
		 * @brief 向传输端提交一条二进制消息。
		 * @details 第 1 个参数（std::span<const std::byte>）为输入。要发送的完整消息。
		 *
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Send(std::span<const std::byte>, std::string& error) = 0;
		/**
		 * @brief 尝试从传输端读取一条二进制消息。
		 * @details 第 1 个参数（NetworkMessage&）为输出。接收消息内容。
		 *
		 * @param[in] available 指明本次是否收到消息。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Receive(NetworkMessage&, bool& available, std::string& error) = 0;
		virtual bool AdvanceTick(uint64_t, std::string& error) {
			error.clear();
			return true;
		}
		/**
		 * @brief 关闭传输端及其本地资源。
		 */
		virtual void Close() noexcept = 0;
	};
} /* 命名空间 Catalpa */
