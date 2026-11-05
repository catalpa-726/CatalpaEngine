#include "catpch.h"
#include "Renderer/Lighting/LightData.h"
#include "Lighting/LightValidation.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace Catalpa {

    Math::MathStatus BuildDirectionalShadowView(const RenderFramePacket& packet, uint32_t resolution,
        DirectionalShadowView& output) noexcept
    {
        if (resolution < 64 || resolution > 8192)
            return { Math::MathError::OutOfRange, "Shadow resolution exceeds the explicit budget" };
        DirectionalShadowView candidate;
        for (size_t index = 0; index < packet.lights.size(); ++index)
            if (packet.lights[index].parameters.type == LightType::Directional && packet.lights[index].parameters.castShadows) {
                candidate.lightIndex = index;
                candidate.enabled = true;
                break;
            }
        Aabb bounds;
        for (const auto& instance : packet.instances)
            if (const auto status = MergeBounds(bounds, instance.worldBounds, bounds); !status.IsSuccess()) return status;
        if (!candidate.enabled || bounds.empty) { output = {}; return {}; }

        // 用double求场景尺度；相机和shadow都使用米制世界坐标，bias由米转换为ZO深度。
        const glm::dvec3 center = (glm::dvec3(bounds.minimum) + glm::dvec3(bounds.maximum)) * 0.5;
        const double radius = std::max(0.01, glm::length(glm::dvec3(bounds.maximum) - center) * 1.05);
        const auto& light = packet.lights[candidate.lightIndex];
        const glm::dvec3 direction(light.direction);
        const glm::dvec3 up = std::abs(direction.y) > 0.95 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 1, 0);
        const auto view = glm::lookAtRH(center - direction * (2.0 * radius), center, up);
        auto projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.01 * radius, 4.0 * radius);
        projection[1][1] = -projection[1][1];
        const auto combined = projection * view;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) {
                const double value = combined[column][row];
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                    return { Math::MathError::NonFinite, "Directional shadow matrix exceeds finite float range" };
                candidate.viewProjection[column][row] = static_cast<float>(value);
            }
        const double normalizedBias = double(light.parameters.shadowBiasMeters) / (3.99 * radius);
        if (!std::isfinite(normalizedBias) || normalizedBias < 0 || normalizedBias > 1)
            return { Math::MathError::OutOfRange, "Shadow bias exceeds the finite light depth span" };
        candidate.normalizedBias = static_cast<float>(normalizedBias);
        output = candidate;
        return {};
    }

    Math::MathStatus BuildLightingData(const RenderFramePacket& packet, const DirectionalShadowView& shadow,
        uint32_t resolution, uint32_t specularMipLevels, PackedLightingData& output) noexcept
    {
        if (packet.lights.size() > MaximumRenderLights || resolution == 0)
            return { Math::MathError::OutOfRange, "Lighting data exceeds the fixed light or shadow budget" };
        try {
            std::string error;
            if (packet.environment && !ValidateEnvironmentSettings(packet.environment->settings, error))
                return { Math::MathError::OutOfRange, "Lighting environment settings are outside their finite domain" };
            for (const auto& light : packet.lights)
                if (!ValidateLightParameters(light.parameters, error))
                    return { Math::MathError::OutOfRange, "Lighting parameters are outside their finite domain" };
        } catch (const std::bad_alloc&) {
            return { Math::MathError::OutOfRange, "Cannot validate lighting parameters" };
        }
        PackedLightingData candidate;
        candidate.countEnvironment[0] = static_cast<float>(packet.lights.size());
        candidate.countEnvironment[1] = 0; // 无环境不冒充有效IBL。
        if (packet.environment) {
            const auto& settings = packet.environment->settings;
            candidate.countEnvironment[1] = settings.intensity;
            candidate.countEnvironment[2] = settings.rotationRadians;
            candidate.countEnvironment[3] = settings.exposureEV;
            candidate.fogColorDensity = { settings.fogColor.x, settings.fogColor.y, settings.fogColor.z,
                settings.fogEnabled ? settings.fogDensityPerMeter : 0 };
        }
        candidate.shadowParameters = { shadow.enabled ? 1.0f : 0.0f, shadow.normalizedBias,
            1.0f / static_cast<float>(resolution), static_cast<float>(specularMipLevels ? specularMipLevels - 1 : 0) };
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                candidate.shadowViewProjection[column][row] = shadow.viewProjection[column][row];
        for (size_t index = 0; index < packet.lights.size(); ++index) {
            const auto& light = packet.lights[index];
            candidate.lights[index] = PackLightData(light.parameters, light.position, light.direction,
                shadow.enabled && shadow.lightIndex == index);
        }
        output = candidate;
        return {};
    }
}
