#pragma once
#include "Audio/AudioTypes.h"
namespace Catalpa {
	/**
	 * @brief 读取音频配置。
	 *
	 * @param[in] text 包含音频服务配置的 JSON 文本。
	 * @param[out] out 接收 AudioServiceConfig 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadAudioConfig(std::string_view, AudioServiceConfig&, std::string& error);
}
