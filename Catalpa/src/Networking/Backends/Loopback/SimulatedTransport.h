#pragma once
#include "Networking/Backends/Loopback/LoopbackTransport.h"
namespace Catalpa {
	/* reorder 给奇数包额外两 tick 抖动，并倒序交付同时到期的包，真正覆盖跨包乱序。 */
	struct LoopbackFaults {
		uint32_t delayTicks = 0, dropEvery = 0, duplicateEvery = 0;
		bool reverseDuePackets = false;
	};
	/**
	 * @brief 构造一对按模拟时钟施加延迟、丢包、重复和乱序的环回端点。
	 * 调试故障按明确 tick 推进，不等待墙钟、不启用真实网络、不隐式重传传输层数据。
	 *
	 * @param[out] first 成功时接收第一个通信端点的所有权。
	 * @param[out] second 与 first 分开存放，成功时接收另一通信端点的所有权。
	 * @param[in] faults 按模拟时钟设置延迟，并指定每隔多少次发送丢弃或重复一个包。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 约束消息大小和队列容量的传输配置。
	 *
	 * @return 传输限制和故障设置合法且两个端点交付成功时为 true，否则为 false。
	 */
	CATALPA_API bool CreateSimulatedLoopbackPair(std::unique_ptr<INetworkTransport>&,
	    std::unique_ptr<INetworkTransport>&, LoopbackFaults, std::string& error, NetworkLimits = {});
} /* 命名空间 Catalpa */
