#pragma once
#include "Networking/ReplicationSchema.h"
#include "Networking/Backends/Loopback/SimulatedTransport.h"
namespace Catalpa {
	struct LoopbackSettings {
		uint32_t tickRate = 60;
		ReplicationLimits limits;
		LoopbackFaults faults;
	};
	/**
	 * @brief 读取本地回环设置。
	 *
	 * @param[in] text 描述本地环回传输配置的 JSON 文本。
	 * @param[out] output 接收 LoopbackSettings 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadLoopbackSettings(std::string_view, LoopbackSettings&, std::string& error);
	/**
	 * @brief 写入本地回环设置。
	 *
	 * @param[in] value 待写入配置文档的本地环回传输设置。
	 * @param[out] output 成功时接收记录环回传输设置的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteLoopbackSettings(const LoopbackSettings&, std::string&, std::string& error);
} /* 命名空间 Catalpa */
