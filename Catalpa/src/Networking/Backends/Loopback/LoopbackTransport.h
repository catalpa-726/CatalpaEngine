#pragma once
#include "Core/Base.h"
#include "Networking/INetworkTransport.h"
namespace Catalpa {
	/**
	 * @brief 创建共享有界邮箱的一对进程内环回端点。
	 * 两个端点共享有界邮箱；只供进程内验证，不能宣称跨机器、加密或可靠重传已实现。
	 *
	 * @param[out] first 成功时接收第一个通信端点的所有权。
	 * @param[out] second 成功时接收与第一个端点相连的另一端。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 约束消息大小和队列容量的传输配置。
	 *
	 * @return 两个端点均构造成功并交付时为 true，否则为 false。
	 */
	CATALPA_API bool CreateLoopbackPair(std::unique_ptr<INetworkTransport>& first,
	    std::unique_ptr<INetworkTransport>& second, std::string& error, NetworkLimits limits = {});
} /* 命名空间 Catalpa */
