#pragma once

#include "Images/ImageAsset.h"
#include "Core/Base.h"

namespace Catalpa {

	/*
	 * 采用 Y 轴向上的右手坐标系，立方体六个面依次为 +X、-X、+Y、-Y、+Z、-Z；面内 s、t 按左上起算的像素中心映射到 [-1,1]。
	 * 各面方向依次为 (1,-t,-s)、(-1,-t,s)、(s,1,t)、(s,-1,-t)、(s,-t,1)、(-s,-t,-1)。
	 * 辐亮度单独派生为立方体贴图，辐照度包含余弦加权积分；镜面反射的 mip 层级对应线性粗糙度，BRDF 查找表为二维。
	 * 源 HDR 经纬图只读；CPU 资产不持有 GPU 纹理或采样器。
	 */
	struct EnvironmentAsset {
		uint32_t schemaVersion = 1;
		uint32_t preprocessingVersion = 2;
		uint32_t sampleCount = 128;
		ImageAsset radiance, irradiance, specular, brdfLut;
	};

	/**
	 * @brief 校验环境资产。
	 *
	 * @param[in] asset 含环境贴图及其预处理结果的待验证资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] CATALPA_API bool ValidateEnvironmentAsset(const EnvironmentAsset&, std::string& error);

}
