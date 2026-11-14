#pragma once

#include "Lighting/LightParameters.h"

#include <array>
#include <cstdint>
#include <cmath>
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

	inline constexpr uint32_t MaximumRenderLights = 32;

	/* Renderer专用显式float4布局，不直接复制GLM或领域对象的内存布局。 */
	struct alignas(16) PackedLightData {
		std::array<float, 4> positionRange{};
		std::array<float, 4> directionOuterCos{};
		std::array<float, 4> colorIntensity{};
		std::array<float, 4> typeInnerCosShadow{};
	};
	struct alignas(16) PackedLightingData {
		std::array<float, 4> countEnvironment{0, 1, 0, 0};
		/* x=shadow有效，y=深度bias(ZO归一化)，z=texel尺寸，w=specular最大mip。 */
		std::array<float, 4> shadowParameters{};
		std::array<std::array<float, 4>, 4> shadowViewProjection{};
		std::array<PackedLightData, MaximumRenderLights> lights{};
		std::array<float, 4> fogColorDensity{}; /* RGB为线性雾色，w为每米密度；0表示关闭。 */
	};
	static_assert(sizeof(PackedLightData) == 64 && sizeof(PackedLightingData) == 2160 &&
	    offsetof(PackedLightingData, shadowViewProjection) == 32 && offsetof(PackedLightingData, lights) == 96 &&
	    offsetof(PackedLightingData, fogColorDensity) == 2144);

	struct DirectionalShadowView {
		bool enabled = false;
		size_t lightIndex = 0;
		Math::Mat4 viewProjection{1.0f};
		float normalizedBias = 0;
	};
	/**
	 * @brief 根据场景包围盒和可投影的方向光建立阴影视图，计算可覆盖场景的矩阵与偏移。
	 * 阴影覆盖所有已校验的世界空间包围盒。阴影方向、零到一深度区间和 Y 轴翻转约定固定，后端不再调整坐标。
	 *
	 * @param[in] packet 包含光源和可投射阴影实例的场景快照。
	 * @param[in] shadowResolution 正方形阴影贴图的边长，单位为像素，取值 64 至 8192。
	 * @param[out] output 成功时接收阴影视图矩阵、方向光索引及启用状态。
	 *
	 * @return 返回阴影视图的数学构建状态；成功后仍须检查输出是否启用阴影。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildDirectionalShadowView(
	    const RenderFramePacket&, uint32_t shadowResolution, DirectionalShadowView& output) noexcept;
	/**
	 * @brief 将本帧光源、阴影与环境采样参数打包为 GPU 输入。
	 *
	 * @param[in] packet 提供所有场景光源的帧快照。
	 * @param[in] shadow 当前方向光的阴影信息。
	 * @param[in] shadowResolution 阴影贴图分辨率，单位为像素。
	 * @param[in] specularMipLevels 环境镜面预过滤纹理的 mip 层数。
	 * @param[out] output 接收打包后的光照数据。
	 *
	 * @return 数学运算的状态，用于区分成功和无效或不可表示的输入。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildLightingData(const RenderFramePacket&, const DirectionalShadowView&,
	    uint32_t shadowResolution, uint32_t specularMipLevels, PackedLightingData& output) noexcept;

	/**
	 * @brief 按着色器的向量布局打包光源位置、方向、衰减范围、颜色和阴影标志。
	 *
	 * @param[in] p 类型、范围、锥角、线性颜色及强度等源光照参数。
	 * @param[in] position 光源位置，以世界空间坐标表示。
	 * @param[in] direction 光源的世界空间照射方向，直接写入打包记录。
	 * @param[in] shadowed 是否在打包记录中启用该光源的阴影采样。
	 *
	 * @return 返回按 GPU 字段顺序排列的光照记录，聚光锥角已转换为余弦值。
	 */
	[[nodiscard]] inline PackedLightData PackLightData(
	    const LightParameters& p, const Math::Vec3& position, const Math::Vec3& direction, bool shadowed) noexcept {
		PackedLightData out;
		out.positionRange = {position.x, position.y, position.z, p.range};
		out.directionOuterCos = {direction.x, direction.y, direction.z, std::cos(p.outerConeRadians)};
		out.colorIntensity = {p.color.x, p.color.y, p.color.z, p.intensity};
		out.typeInnerCosShadow = {static_cast<float>(p.type), std::cos(p.innerConeRadians), shadowed ? 1.0f : 0.0f, 0};
		return out;
	}

}
