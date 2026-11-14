#pragma once

#include "Materials/MaterialInstance.h"

namespace Catalpa {

	inline constexpr uint32_t DefaultMaterialSamplerFlags = 240;

	/* 领域参数到表面着色的受检值；CPU结构不是GPU块，renderer显式编码五个float4。 */
	struct MaterialSurfaceParameters {
		std::array<float, 4> baseColor{1, 1, 1, 1};
		std::array<float, 3> emissive{0, 0, 0};
		float metallic = 0, roughness = 0.5f, normalScale = 1, occlusionStrength = 1, alphaCutoff = 0.5f;
		std::array<float, 4> uvScaleOffset{1, 1, 0, 0};
		/* 每4096模拟秒的整数频率（可为负）；UV作幅度0.5的正弦摆动，各寻址模式下wrap连续。 */
		std::array<float, 2> uvCycles{0, 0};
		uint32_t pulseCycles = 0;
		bool flipV = false;
		std::array<AssetHandle<ImageAsset>, 5>
		    textures{}; /* 依次为基础颜色、法线、金属度与粗糙度、自发光、环境遮蔽。 */
		/* 第 0–1、2–3 位分别指定 U、V 寻址模式（0 重复、1 钳制、2 镜像）；第 4–7 位依次控制缩小线性过滤、放大线性过滤、层间线性过滤和启用 mip 层级。 */
		std::array<uint32_t, 5> samplerFlags{240, 240, 240, 240, 240};
	};

	/**
	 * @brief 解析材质渲染表面。
	 *
	 * @param[in] instance 提供材质定义、参数默认值和实例覆盖值的材质实例。
	 * @param[out] output 接收 MaterialSurfaceParameters 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] CATALPA_API bool ResolveMaterialSurface(
	    const MaterialInstance&, MaterialSurfaceParameters& output, std::string& error);

}
