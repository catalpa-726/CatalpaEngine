#include "catpch.h"

#include "TestRunner.h"
#include "Lighting/LightValidation.h"
#include "Lighting/EnvironmentAssetLoader.h"
#include "AssetPipeline/Environment/EnvironmentPreprocessor.h"
#include "Materials/MaterialSurface.h"
#include "Materials/MaterialValidation.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include "AssetPipeline/Importers/TextureImporter.h"

namespace Catalpa::Tests {
	namespace {

		/**
		 * @brief 数学常量测试夹具独立于用户素材，HDR 像素与原始资产目录隔离。
		 *
		 * @return 各像素线性 RGB 均为 (2, 1, 0.5) 的 4×2 浮点环境图。
		 */
		ImageAsset ConstantEnvironment() {
			ImageAsset image;
			image.sourceUri = "fixture://constant-radiance";
			auto& data = image.image;
			data.format = PixelFormat::RGBA32_Float;
			data.width = 4;
			data.height = 2;
			data.pixels.resize(4 * 2 * 16);
			data.subresources.push_back({0, 0, 0, 4, 2, 1, 0, 64, 128, 128});
			const std::array<float, 4> color{2, 1, 0.5f, 1};
			for (size_t i = 0; i < 8; ++i)
				std::memcpy(data.pixels.data() + i * 16, color.data(), 16);
			return image;
		}

		std::array<float, 4> FirstPixel(const ImageAsset& image) {
			std::array<float, 4> pixel{};
			if (image.image.pixels.size() >= 16)
				std::memcpy(pixel.data(), image.image.pixels.data(), 16);
			return pixel;
		}

	}

	void RunLightingTests(TestContext& test) {
		std::string error;
		LightParameters light;
		test.Check(
		    ValidateLightParameters(light, error), "Directional defaults have explicit lux and metre/radian semantics");
		light.intensity = -1;
		test.Check(!ValidateLightParameters(light, error), "Negative light intensity is rejected");
		light.intensity = std::numeric_limits<float>::infinity();
		test.Check(!ValidateLightParameters(light, error), "Infinite light intensity is rejected");
		light.intensity = 1;
		light.innerConeRadians = light.outerConeRadians + 0.1f;
		test.Check(!ValidateLightParameters(light, error), "Inner cone cannot exceed the outer cone");
		light.innerConeRadians = 0;
		light.outerConeRadians = LightingPi / 2;
		test.Check(!ValidateLightParameters(light, error), "Spot cone reaching pi/2 is rejected");
		test.Check(std::abs(LumensToIsotropicCandela(4 * LightingPi) - 1) < 1e-6f,
		    "Explicit isotropic lumen conversion yields candela");
		EnvironmentSettings environmentSettings;
		environmentSettings.fogEnabled = true;
		environmentSettings.fogDensityPerMeter = 0.1f;
		test.Check(ValidateEnvironmentSettings(environmentSettings, error),
		    "Uniform fog uses nonnegative inverse-metre density and linear color");
		environmentSettings.fogDensityPerMeter = -0.1f;
		test.Check(!ValidateEnvironmentSettings(environmentSettings, error),
		    "Negative extinction cannot create exponentially growing fog");
		environmentSettings.fogDensityPerMeter = 0;
		environmentSettings.fogColor.x = std::numeric_limits<float>::infinity();
		test.Check(!ValidateEnvironmentSettings(environmentSettings, error),
		    "Non-finite fog color is rejected even while density is zero");

		MaterialParameter cycles;
		cycles.id = "uv_cycles";
		cycles.type = MaterialParameterType::Float2;
		test.Check(ValidateMaterialValue(cycles, std::array<float, 2>{-16, 32}, error),
		    "Signed integer UV cycles are accepted");
		test.Check(!ValidateMaterialValue(cycles, std::array<float, 2>{0.5f, 1}, error),
		    "Fractional UV cycles cannot break the 4096-second wrap contract");
		MaterialParameter alpha;
		alpha.id = "base_color";
		alpha.type = MaterialParameterType::Float4;
		test.Check(ValidateMaterialValue(alpha, std::array<float, 4>{8, 4, 2, 1}, error),
		    "Linear HDR base RGB is independent of bounded coverage alpha");
		test.Check(!ValidateMaterialValue(alpha, std::array<float, 4>{1, 1, 1, 1.1f}, error),
		    "Coverage alpha above one is rejected without schema max");
		MaterialParameter cutoff;
		cutoff.id = "alpha_cutoff";
		cutoff.type = MaterialParameterType::Float;
		test.Check(ValidateMaterialValue(cutoff, 2.0f, error),
		    "glTF alpha cutoff above one is valid and discards every Mask fragment");
		test.Check(!ValidateMaterialValue(cutoff, -0.1f, error),
		    "Negative alpha cutoff is rejected without clamping the source");
		uint32_t sampler = 0;
		test.Check(Tools::MaterialImporter::SamplerFlags(10497, 10497, 9987, 9729, sampler, error) && sampler == 240,
		    "Repeat and linear min/mag/mip source sampler maps to the ordinary material default");
		test.Check(Tools::MaterialImporter::SamplerFlags(33071, 33648, 9728, 9728, sampler, error) && sampler == 9,
		    "Clamp U, mirrored V and nearest source filters are preserved independently");
		const auto oldSampler = sampler;
		test.Check(
		    !Tools::MaterialImporter::SamplerFlags(0, 10497, 9729, 9729, sampler, error) && sampler == oldSampler,
		    "Unknown source sampler does not silently replace the previous policy");
		MaterialParameter samplerParameter;
		samplerParameter.id = "baseColor_sampler";
		samplerParameter.type = MaterialParameterType::UInt;
		test.Check(ValidateMaterialValue(samplerParameter, uint32_t(240), error) &&
		        !ValidateMaterialValue(samplerParameter, uint32_t(255), error),
		    "Material sampler validates the stable bit protocol and rejects reserved wrap codes");
		std::shared_ptr<const MaterialDefinition> pbr;
		test.Check(CreateBuiltinMaterialDefinition(BuiltinMaterialKind::Pbr, AssetId::FromWords(1, 2), pbr, error),
		    "PBR fixture uses the shared production schema");
		if (pbr) {
			auto invalid = *pbr;
			invalid.parameters.front().defaultValue = std::array<float, 4>{2, 1, 1, 1};
			test.Check(!ValidateMaterialDefinition(invalid, error),
			    "PBR reflectance above one is rejected; HDR energy uses emissive");
			invalid.shadingModel = MaterialShadingModel::Unlit;
			test.Check(ValidateMaterialDefinition(invalid, error),
			    "Unlit linear output can retain HDR RGB independently of PBR reflectance");
		}
		std::shared_ptr<const MaterialDefinition> unlit;
		if (CreateBuiltinMaterialDefinition(BuiltinMaterialKind::Unlit, AssetId::FromWords(2, 1), unlit, error)) {
			auto definition = *unlit;
			MaterialParameter texture;
			texture.id = "baseColor_texture";
			texture.type = MaterialParameterType::Texture2D;
			texture.textureSemantic = MaterialTextureSemantic::BaseColor;
			const auto textureId = AssetId::FromWords(2, 2);
			texture.defaultValue = AssetHandle<ImageAsset>{textureId};
			definition.parameters.push_back(texture);
			AssetVersionRef<MaterialDefinition> fixedDefinition;
			const bool fixed = AssetVersionRef<MaterialDefinition>::TryCreate(AssetId::FromWords(2, 3), 1,
			    std::make_shared<const MaterialDefinition>(std::move(definition)), fixedDefinition, error);
			std::shared_ptr<const MaterialInstance> draft;
			if (fixed && CreateMaterialInstance(fixedDefinition, {}, {}, 1, draft, error)) {
				MaterialInstance resolved = *draft;
				auto image = std::make_shared<const ImageAsset>(ConstantEnvironment());
				const bool bound =
				    AssetVersionRef<ImageAsset>::TryCreate(textureId, 7, image, resolved.textureVersions[0], error);
				std::shared_ptr<const MaterialInstance> edited;
				const std::array change{MaterialParameterOverride{"base_color", std::array<float, 4>{0.5f, 1, 1, 1}}};
				test.Check(bound && UpdateMaterialInstance(resolved, change, {}, edited, error) &&
				        edited->textureVersions[0].GetVersion() == 7 && edited->textureVersions[0].Get() == image.get(),
				    "Editing scalar material values preserves the exact owned image version");
				const auto held = edited;
				const std::array textureChange{
				    MaterialParameterOverride{"baseColor_texture", AssetHandle<ImageAsset>{AssetId::FromWords(2, 4)}}};
				test.Check(UpdateMaterialInstance(resolved, textureChange, {}, edited, error) &&
				        !edited->textureVersions[0].HasValue() && held && held->textureVersions[0].GetVersion() == 7,
				    "Changing a texture handle requires new fixed resolution and leaves the old immutable version alive");
			} else
				test.Check(false, "Material texture-version fixture creates a valid production instance");
		} else
			test.Check(false, "Texture-version fixture creates the builtin Unlit schema");
	}

	void RunEnvironmentPreprocessTests(TestContext& test) {
		/* 仅验证准入算术；不为边界测试分配 8K HDR 的大块像素。 */
		std::string error;
		size_t decodedBytes = 0;
		test.Check(Tools::TextureImporter::HdrDecodedSize(8192, 4096, decodedBytes, error) &&
		        decodedBytes == 512ull * 1024 * 1024,
		    "An 8K RGBA32F HDR fits the inclusive 512 MiB image limit");
		const size_t acceptedBytes = decodedBytes;
		test.Check(
		    !Tools::TextureImporter::HdrDecodedSize(8192, 4097, decodedBytes, error) && decodedBytes == acceptedBytes,
		    "HDR dimensions over 512 MiB fail without replacing the accepted size");
		test.Check(!Tools::TextureImporter::HdrDecodedSize(UINT32_MAX, UINT32_MAX, decodedBytes, error) &&
		        decodedBytes == acceptedBytes,
		    "Extreme HDR dimensions cannot overflow into an accepted byte size");
		test.Check(!Tools::TextureImporter::HdrDecodedSize(0, 4096, decodedBytes, error),
		    "An empty HDR dimension is rejected before decoding");

		const auto source = ConstantEnvironment();
		const auto originalBytes = source.image.pixels;
		Tools::EnvironmentPreprocessSettings settings;
		settings.radianceFaceSize = 2;
		settings.irradianceFaceSize = 2;
		settings.specularFaceSize = 2;
		settings.brdfSize = 4;
		settings.sampleCount = 32;
		EnvironmentAsset environment;
		const bool built = Tools::EnvironmentPreprocessor::Build(source, settings, {}, environment, error);
		test.Check(built, "CPU environment preprocessor produces owned irradiance, specular mips and BRDF LUT");
		test.Check(source.image.pixels == originalBytes, "Environment integration preserves the source bytes");
		if (!built)
			return;

		const auto irradiance = FirstPixel(environment.irradiance);
		test.Check(std::abs(irradiance[0] - 2 * LightingPi) < 1e-4f && std::abs(irradiance[1] - LightingPi) < 1e-4f,
		    "Constant radiance integrates to irradiance pi times radiance, not unweighted hemisphere average");
		const auto specular = FirstPixel(environment.specular);
		test.Check(std::abs(specular[0] - 2) < 1e-4f && std::abs(specular[2] - 0.5f) < 1e-4f,
		    "GGX prefilter preserves a constant radiance field");
		test.Check(environment.specular.image.mipLevels == 2 && environment.specular.image.subresources.size() == 12 &&
		        environment.radiance.image.dimension == ImageDimension::Cube &&
		        ValidateEnvironmentAsset(environment, error),
		    "Specular chain reaches 1x1 and retains explicit per-mip layouts");

		std::vector<std::byte> payload;
		test.Check(EnvironmentAssetLoader::EncodePayload(environment, payload, error),
		    "Runtime and cooker share environment encoding");
		AssetDecodeContext context;
		context.maxResidentBytes = 8 * 1024 * 1024;
		AssetLoadResult decoded;
		test.Check(EnvironmentAssetLoader::DecodePayload(payload, context, decoded, error),
		    "Environment payload round-trips through the production loader");
		if (decoded.value) {
			const auto value = std::static_pointer_cast<const EnvironmentAsset>(decoded.value);
			const auto radiance = FirstPixel(value->radiance);
			test.Check(std::abs(radiance[0] - 2) < 1e-6f && std::abs(radiance[1] - 1) < 1e-6f,
			    "Cube Cook/load retains constant HDR radiance without gamma or normalization");
		}
		const auto previous = decoded.value;
		payload.push_back(std::byte{0});
		test.Check(
		    !EnvironmentAssetLoader::DecodePayload(payload, context, decoded, error) && decoded.value == previous,
		    "Malformed environment leaves the published output unchanged");

		CancellationSource cancellation;
		cancellation.Cancel();
		const auto oldSource = environment.radiance.sourceUri;
		test.Check(!Tools::EnvironmentPreprocessor::Build(source, settings, cancellation.Token(), environment, error) &&
		        environment.radiance.sourceUri == oldSource,
		    "Cancelled preprocessing preserves the previous environment");
		auto invalid = source;
		const float negative = -1;
		std::memcpy(invalid.image.pixels.data(), &negative, sizeof(negative));
		test.Check(!Tools::EnvironmentPreprocessor::Build(invalid, settings, {}, environment, error),
		    "Negative source radiance fails explicitly instead of clamping the raw image");

		/* 非对称方向场区分六面朝向，常量环境不能发现交换轴或翻转Y的问题。 */
		auto oriented = source;
		for (uint32_t y = 0; y < 2; ++y) {
			for (uint32_t x = 0; x < 4; ++x) {
				const float phi = ((float(x) + 0.5f) / 4 - 0.5f) * 2 * LightingPi;
				const float theta = (float(y) + 0.5f) / 2 * LightingPi;
				const std::array<float, 4> value{
				    1 + std::cos(phi) * std::sin(theta), 1 + std::cos(theta), 1 + std::sin(phi) * std::sin(theta), 1};
				std::memcpy(oriented.image.pixels.data() + (y * 4 + x) * 16, value.data(), 16);
			}
		}
		settings.radianceFaceSize = 1;
		EnvironmentAsset directional;
		const bool orientedBuilt = Tools::EnvironmentPreprocessor::Build(oriented, settings, {}, directional, error);
		test.Check(orientedBuilt, "A direction-encoded equirectangular source converts to six Cube faces");
		if (orientedBuilt) {
			const auto component = [&](size_t face, size_t channel) {
				float value;
				std::memcpy(&value,
				    directional.radiance.image.pixels.data() + directional.radiance.image.subresources[face].offset +
				        channel * 4,
				    4);
				return value;
			};
			test.Check(component(0, 0) > component(1, 0) && component(2, 1) > component(3, 1) &&
			        component(4, 2) > component(5, 2),
			    "Cube face order is +X/-X/+Y/-Y/+Z/-Z in the right-handed Y-up convention");
		}
	}

}
