#pragma once

#include "Lighting/LightParameters.h"
#include <array>
#include <cstdint>
#include <cmath>
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

    inline constexpr uint32_t MaximumRenderLights = 32;

    // Renderer专用显式float4布局，不直接复制GLM或领域对象的内存布局。
    struct alignas(16) PackedLightData {
        std::array<float, 4> positionRange{};
        std::array<float, 4> directionOuterCos{};
        std::array<float, 4> colorIntensity{};
        std::array<float, 4> typeInnerCosShadow{};
    };
    struct alignas(16) PackedLightingData {
        std::array<float, 4> countEnvironment{0, 1, 0, 0};
        // x=shadow有效，y=深度bias(ZO归一化)，z=texel尺寸，w=specular最大mip。
        std::array<float, 4> shadowParameters{};
        std::array<std::array<float, 4>, 4> shadowViewProjection{};
        std::array<PackedLightData, MaximumRenderLights> lights{};
        std::array<float, 4> fogColorDensity{}; // RGB为线性雾色，w为每米密度；0表示关闭。
    };
    static_assert(sizeof(PackedLightData) == 64 && sizeof(PackedLightingData) == 2160
        && offsetof(PackedLightingData, shadowViewProjection) == 32
        && offsetof(PackedLightingData, lights) == 96 && offsetof(PackedLightingData, fogColorDensity) == 2144);

    struct DirectionalShadowView {
        bool enabled = false;
        size_t lightIndex = 0;
        Math::Mat4 viewProjection{ 1.0f };
        float normalizedBias = 0;
    };
    // 覆盖所有受检世界bounds；阴影方向/ZO/Y翻转固定，不依赖隐藏的后端坐标修正。
    [[nodiscard]] CATALPA_API Math::MathStatus BuildDirectionalShadowView(const RenderFramePacket&,
        uint32_t shadowResolution, DirectionalShadowView& output) noexcept;
    [[nodiscard]] CATALPA_API Math::MathStatus BuildLightingData(const RenderFramePacket&,
        const DirectionalShadowView&, uint32_t shadowResolution, uint32_t specularMipLevels,
        PackedLightingData& output) noexcept;

    [[nodiscard]] inline PackedLightData PackLightData(const LightParameters& p,
        const Math::Vec3& position, const Math::Vec3& direction, bool shadowed) noexcept {
        PackedLightData out;
        out.positionRange = {position.x, position.y, position.z, p.range};
        out.directionOuterCos = {direction.x, direction.y, direction.z, std::cos(p.outerConeRadians)};
        out.colorIntensity = {p.color.x, p.color.y, p.color.z, p.intensity};
        out.typeInnerCosShadow = {static_cast<float>(p.type), std::cos(p.innerConeRadians), shadowed ? 1.0f : 0.0f, 0};
        return out;
    }

}
