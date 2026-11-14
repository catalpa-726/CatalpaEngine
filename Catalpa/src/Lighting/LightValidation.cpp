#include "catpch.h"

#include "Lighting/LightValidation.h"

namespace Catalpa {

	bool TryGetWorldLightDirection(const Math::Mat4& world, Math::Vec3& output) noexcept {
		/* float 世界基向量本身有限时，求长度的平方仍可能超出 float。 */
		const glm::dvec3 forward = -glm::dvec3(world[2]);
		const double length = glm::length(forward);
		if (!std::isfinite(length) || length <= 1.0e-8)
			return false;
		output = Math::Vec3(forward / length);
		return true;
	}

	bool ValidateEnvironmentSettings(const EnvironmentSettings& settings, std::string& error) {
		if (!std::isfinite(settings.intensity) || settings.intensity < 0 || settings.intensity > 1e6f ||
		    !std::isfinite(settings.rotationRadians) || !std::isfinite(settings.exposureEV) ||
		    settings.exposureEV < -32 || settings.exposureEV > 32) {
			error = "Environment requires intensity in [0,1e6], finite rotation and exposure EV in [-32,32]";
			return false;
		}
		if (!std::isfinite(settings.fogDensityPerMeter) || settings.fogDensityPerMeter < 0 ||
		    settings.fogDensityPerMeter > 1e6f) {
			error = "Uniform fog density requires finite inverse metres in [0,1e6]";
			return false;
		}
		for (int axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(settings.fogColor[axis]) || settings.fogColor[axis] < 0 ||
			    settings.fogColor[axis] > 1e8f) {
				error = "Uniform fog color exceeds the nonnegative linear RGB budget";
				return false;
			}
		}
		error.clear();
		return true;
	}

	bool ValidateLightParameters(const LightParameters& light, std::string& error) {
		if (light.type != LightType::Directional && light.type != LightType::Point && light.type != LightType::Spot) {
			error = "Unknown light type";
			return false;
		}
		for (int i = 0; i < 3; ++i) {
			if (!std::isfinite(light.color[i]) || light.color[i] < 0) {
				error = "Light color must be nonnegative finite linear RGB";
				return false;
			}
		}
		if (!std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) || light.range <= 0 ||
		    !std::isfinite(light.shadowBiasMeters) || light.shadowBiasMeters < 0) {
			error = "Light intensity, metre range or shadow bias is invalid";
			return false;
		}
		/* 点光在1mm的显式有限距离下限和最多32光源下仍不能溢出float累积。 */
		const double maximumColor = std::max({double(light.color.x), double(light.color.y), double(light.color.z)});
		if (maximumColor * double(light.intensity) > 1e12) {
			error = "Light color-intensity product exceeds the explicit 1e12 GPU lighting budget";
			return false;
		}
		if (!std::isfinite(light.innerConeRadians) || !std::isfinite(light.outerConeRadians) ||
		    light.innerConeRadians < 0 || light.innerConeRadians > light.outerConeRadians ||
		    light.outerConeRadians <= 0 || light.outerConeRadians >= LightingPi / 2.0f) {
			error = "Spot cone angles require 0 <= inner <= outer < pi/2 radians";
			return false;
		}
		error.clear();
		return true;
	}

}
