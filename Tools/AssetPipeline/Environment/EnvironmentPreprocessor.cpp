#include "catpch.h"

#include "AssetPipeline/Environment/EnvironmentPreprocessor.h"

#include "Lighting/LightUnits.h"
#include "Core/Math/MathTypes.h"
#include "Images/ImageValidation.h"

namespace Catalpa::Tools {
	namespace {

		using Vec3 = Math::Vec3;

		float RadicalInverse(uint32_t bits) {
			bits = (bits << 16) | (bits >> 16);
			bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
			bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
			bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
			bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
			return static_cast<float>(bits) * 2.3283064365386963e-10f;
		}

		Vec3 CubeDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size) {
			const float s = 2 * (float(x) + 0.5f) / float(size) - 1;
			const float t = 2 * (float(y) + 0.5f) / float(size) - 1;
			Vec3 direction;
			switch (face) {
			case 0:
				direction = {1, -t, -s};
				break;
			case 1:
				direction = {-1, -t, s};
				break;
			case 2:
				direction = {s, 1, t};
				break;
			case 3:
				direction = {s, -1, -t};
				break;
			case 4:
				direction = {s, -t, 1};
				break;
			default:
				direction = {-s, -t, -1};
				break;
			}
			return glm::normalize(direction);
		}

		/**
		 * @brief 将世界方向映射到经纬度图像并双线性采样辐亮度。
		 * u周期环绕、v在南北极夹取；使用输入像素中心定义，保持经纬度朝向一致。
		 *
		 * @param[in] image 以经纬度展开的环境图及其子资源布局。
		 * @param[in] direction 查找环境辐亮度的单位方向，以 Y 轴为极轴。
		 *
		 * @return 按方向双线性采样得到的线性 RGB 辐亮度。
		 */
		Vec3 Sample(const ImageData& image, const Vec3& direction) {
			const float u = std::atan2(direction.z, direction.x) / (2 * LightingPi) + 0.5f;
			const float v = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / LightingPi;
			const float px = u * image.width - 0.5f, py = v * image.height - 0.5f;
			const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
			const float tx = px - float(x0), ty = py - float(y0);
			const auto& sub = image.subresources.front();
			const auto texel = [&](int x, int y) {
				x = (x % int(image.width) + int(image.width)) % int(image.width);
				y = std::clamp(y, 0, int(image.height) - 1);
				std::array<float, 4> pixel;
				std::memcpy(
				    pixel.data(), image.pixels.data() + sub.offset + uint64_t(y) * sub.rowPitch + uint64_t(x) * 16, 16);
				return Vec3(pixel[0], pixel[1], pixel[2]);
			};
			return glm::mix(glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx),
			    glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx), ty);
		}

		Vec3 ToWorld(const Vec3& local, const Vec3& normal) {
			const Vec3 up = std::abs(normal.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
			const Vec3 tangent = glm::normalize(glm::cross(up, normal));
			return tangent * local.x + glm::cross(normal, tangent) * local.y + normal * local.z;
		}

		Vec3 ImportanceGGX(float x, float y, float roughness, const Vec3& normal) {
			const float a = std::max(roughness * roughness, 0.000001f);
			const float phi = 2 * LightingPi * x;
			const float cosTheta = std::sqrt((1 - y) / (1 + (a * a - 1) * y));
			const float sinTheta = std::sqrt(std::max(0.0f, 1 - cosTheta * cosTheta));
			return ToWorld({std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta}, normal);
		}

		ImageAsset FloatImage(
		    std::string source, uint32_t width, uint32_t height, uint32_t levels = 1, bool cube = false) {
			ImageAsset result;
			result.sourceUri = std::move(source);
			auto& image = result.image;
			image.width = width;
			image.height = height;
			image.mipLevels = levels;
			image.format = PixelFormat::RGBA32_Float;
			image.semantic = ImageSemantic::Data;
			image.dimension = cube ? ImageDimension::Cube : ImageDimension::Texture2D;
			uint64_t offset = 0;
			for (uint32_t face = 0; face < (cube ? 6u : 1u); ++face) {
				for (uint32_t mip = 0; mip < levels; ++mip) {
					const uint32_t w = std::max(1u, width >> mip), h = std::max(1u, height >> mip);
					const uint64_t size = uint64_t(w) * h * 16;
					image.subresources.push_back({mip, 0, face, w, h, 1, offset, uint64_t(w) * 16, size, size});
					offset += size;
				}
			}
			image.pixels.resize(static_cast<size_t>(offset));
			return result;
		}

		void Store(ImageData& image, const ImageSubresource& sub, uint32_t x, uint32_t y, const Vec3& rgb) {
			const std::array<float, 4> value{rgb.x, rgb.y, rgb.z, 1.0f};
			std::memcpy(
			    image.pixels.data() + sub.offset + uint64_t(y) * sub.rowPitch + uint64_t(x) * 16, value.data(), 16);
		}

		float GeometrySchlick(float ndot, float roughness) {
			const float k = roughness * roughness * 0.5f;
			return ndot / std::max(ndot * (1 - k) + k, 1e-7f);
		}

	}

	bool EnvironmentPreprocessor::Build(const ImageAsset& source, const EnvironmentPreprocessSettings& settings,
	    CancellationToken cancellation, EnvironmentAsset& output, std::string& error) {
		if (cancellation.IsCancellationRequested()) {
			error = "Environment preprocessing cancelled";
			return false;
		}
		if (!ValidateImageAsset(source, error))
			return false;
		const auto& input = source.image;
		if (input.format != PixelFormat::RGBA32_Float || input.dimension != ImageDimension::Texture2D ||
		    input.mipLevels != 1 || input.arrayLayers != 1 || input.colorSpace != ImageColorSpace::Linear ||
		    input.width != 2 * uint64_t(input.height) || settings.irradianceFaceSize < 1 ||
		    settings.irradianceFaceSize > 128 || settings.radianceFaceSize < 1 || settings.radianceFaceSize > 512 ||
		    settings.specularFaceSize < 1 || settings.specularFaceSize > 512 ||
		    (settings.specularFaceSize & (settings.specularFaceSize - 1)) != 0 || settings.brdfSize < 2 ||
		    settings.brdfSize > 256 || settings.sampleCount < 16 || settings.sampleCount > 4096) {
			error = "IBL requires linear 2:1 RGBA32F input and bounded power-of-two specular settings";
			return false;
		}
		/* 源复制、积分结果及编码暂存共用显式预算；不对大图进行静默缩放。 */
		const uint64_t derivedBytes = uint64_t(settings.radianceFaceSize) * settings.radianceFaceSize * 96 +
		    uint64_t(settings.irradianceFaceSize) * settings.irradianceFaceSize * 96 +
		    uint64_t(settings.specularFaceSize) * settings.specularFaceSize * 128 +
		    uint64_t(settings.brdfSize) * settings.brdfSize * 16;
		if (uint64_t(input.pixels.size()) * 2 + derivedBytes * 2 > settings.maxWorkingBytes) {
			error = "Environment source and derived buffers exceed CPU working budget";
			return false;
		}
		/* 大 HDR 的辐亮度检查也分段响应取消，关闭窗口不必等待整张 512 MiB 扫描。 */
		constexpr size_t ValidationChunkBytes = 1024 * 1024;
		for (size_t begin = 0; begin < input.pixels.size(); begin += ValidationChunkBytes) {
			if (cancellation.IsCancellationRequested()) {
				error = "Environment preprocessing cancelled";
				return false;
			}
			const size_t end = std::min(input.pixels.size(), begin + ValidationChunkBytes);
			for (size_t offset = begin; offset + 4 <= end; offset += 4) {
				float value;
				std::memcpy(&value, input.pixels.data() + offset, 4);
				if (!std::isfinite(value) || value < 0 || value > 1e12f) {
					error = "HDR radiance must be finite and in the explicit [0,1e12] integration budget";
					return false;
				}
			}
		}
		try {
			EnvironmentAsset candidate;
			candidate.sampleCount = settings.sampleCount;
			candidate.radiance = FloatImage(
			    source.sourceUri + "#radiance-cube", settings.radianceFaceSize, settings.radianceFaceSize, 1, true);
			candidate.irradiance = FloatImage(source.sourceUri + "#irradiance-cube", settings.irradianceFaceSize,
			    settings.irradianceFaceSize, 1, true);
			uint32_t levels = 1;
			for (uint32_t size = settings.specularFaceSize; size > 1; size >>= 1)
				++levels;
			candidate.specular = FloatImage(source.sourceUri + "#specular-cube", settings.specularFaceSize,
			    settings.specularFaceSize, levels, true);
			candidate.brdfLut = FloatImage(source.sourceUri + "#brdf-lut", settings.brdfSize, settings.brdfSize);

			const auto cancelled = [&] {
				if (!cancellation.IsCancellationRequested())
					return false;
				error = "Environment preprocessing cancelled";
				return true;
			};
			auto& radiance = candidate.radiance.image;
			for (const auto& sub : radiance.subresources) {
				for (uint32_t y = 0; y < sub.height; ++y) {
					if (cancelled())
						return false;
					for (uint32_t x = 0; x < sub.width; ++x)
						Store(radiance, sub, x, y, Sample(input, CubeDirection(sub.face, x, y, sub.width)));
				}
			}
			auto& irradiance = candidate.irradiance.image;
			for (const auto& irrSub : irradiance.subresources) {
				for (uint32_t y = 0; y < irrSub.height; ++y) {
					if (cancelled())
						return false;
					for (uint32_t x = 0; x < irrSub.width; ++x) {
						const Vec3 normal = CubeDirection(irrSub.face, x, y, irrSub.width);
						Vec3 sum(0);
						for (uint32_t i = 0; i < settings.sampleCount; ++i) {
							const float a = (float(i) + 0.5f) / settings.sampleCount, b = RadicalInverse(i);
							const float r = std::sqrt(a), phi = 2 * LightingPi * b;
							sum += Sample(
							    input, ToWorld({r * std::cos(phi), r * std::sin(phi), std::sqrt(1 - a)}, normal));
						}
						Store(irradiance, irrSub, x, y, sum * (LightingPi / float(settings.sampleCount)));
					}
				}
			}

			auto& specular = candidate.specular.image;
			for (const auto& sub : specular.subresources) {
				const float roughness = levels > 1 ? float(sub.mip) / float(levels - 1) : 0;
				for (uint32_t y = 0; y < sub.height; ++y) {
					if (cancelled())
						return false;
					for (uint32_t x = 0; x < sub.width; ++x) {
						const Vec3 normal = CubeDirection(sub.face, x, y, sub.width);
						Vec3 sum(0);
						float weight = 0;
						for (uint32_t i = 0; i < settings.sampleCount; ++i) {
							const Vec3 half = ImportanceGGX(
							    (float(i) + 0.5f) / settings.sampleCount, RadicalInverse(i), roughness, normal);
							const Vec3 light = 2 * glm::dot(normal, half) * half - normal;
							const float ndotl = std::max(0.0f, glm::dot(normal, light));
							sum += Sample(input, light) * ndotl;
							weight += ndotl;
						}
						Store(specular, sub, x, y, sum / std::max(weight, 1e-7f));
					}
				}
			}

			auto& lut = candidate.brdfLut.image;
			const auto& lutSub = lut.subresources.front();
			for (uint32_t y = 0; y < lut.height; ++y) {
				if (cancelled())
					return false;
				const float roughness = (float(y) + 0.5f) / lut.height;
				for (uint32_t x = 0; x < lut.width; ++x) {
					const float ndotv = (float(x) + 0.5f) / lut.width;
					const Vec3 view(std::sqrt(1 - ndotv * ndotv), 0, ndotv);
					float a = 0, b = 0;
					for (uint32_t i = 0; i < settings.sampleCount; ++i) {
						const Vec3 half = ImportanceGGX(
						    (float(i) + 0.5f) / settings.sampleCount, RadicalInverse(i), roughness, {0, 0, 1});
						const float vdoth = std::max(0.0f, glm::dot(view, half));
						const Vec3 light = 2 * vdoth * half - view;
						if (light.z <= 0)
							continue;
						const float g = GeometrySchlick(ndotv, roughness) * GeometrySchlick(light.z, roughness);
						const float visible = g * vdoth / std::max(half.z * ndotv, 1e-7f);
						const float fresnel = std::pow(1 - vdoth, 5.0f);
						a += (1 - fresnel) * visible;
						b += fresnel * visible;
					}
					Store(lut, lutSub, x, y, {a / settings.sampleCount, b / settings.sampleCount, 0});
				}
			}
			if (!ValidateEnvironmentAsset(candidate, error) || cancelled())
				return false;
			output = std::move(candidate);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

}
