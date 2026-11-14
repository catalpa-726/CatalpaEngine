#pragma once
#include "Audio/Backends/IAudioBackend.h"
namespace Catalpa {
	/**
	 * @brief 创建 miniaudio 后端。
	 *
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 初始化成功的音频后端的独占所有权，初始化失败时为空。
	 */
	std::unique_ptr<IAudioBackend> CreateMiniaudioBackend(std::string& error);
}
