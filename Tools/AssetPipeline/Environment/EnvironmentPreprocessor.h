#pragma once

#include "Lighting/EnvironmentAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {

	struct EnvironmentPreprocessSettings {
		uint32_t radianceFaceSize = 256;
		uint32_t irradianceFaceSize = 16;
		uint32_t specularFaceSize = 64;
		uint32_t brdfSize = 64;
		uint32_t sampleCount = 128;
		size_t maxWorkingBytes = 256 * 1024 * 1024;
	};

	/* 由线性 RGBA32F 格式的 HDR 经纬图派生六面立方体环境光照贴图，保留源图。Hammersley 积分在 CPU 执行。 */
	class EnvironmentPreprocessor final {
	public:
		/**
		 * @brief 从源环境图预计算用于天空和基于图像光照的资源。
		 *
		 * @param[in] source 输入的 ImageAsset。
		 * @param[in] settings 本次操作使用的 EnvironmentPreprocessSettings。
		 * @param[in] cancellation 用于查询取消请求的令牌。
		 * @param[out] output 接收 EnvironmentAsset 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Build(const ImageAsset& source, const EnvironmentPreprocessSettings&, CancellationToken,
		    EnvironmentAsset& output, std::string& error);
	};

}
