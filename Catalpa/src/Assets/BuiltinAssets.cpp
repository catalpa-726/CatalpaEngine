#include "catpch.h"

#include "Assets/BuiltinAssets.h"
#include "Geometry/PrimitiveMesh.h"
#include "Geometry/MeshAsset.h"
#include "Materials/MaterialSurface.h"

#include "Assets/BuiltinAssetIds.h"
#include "Assets/AssetManager.h"
#include "Images/ImageAsset.h"
#include "Images/ImageValidation.h"
#include "Materials/MaterialAssetLoader.h"
#include "Materials/MaterialValidation.h"
#include "Shaders/ShaderLibrary.h"
#include "Physics/CollisionShapeAsset.h"
namespace Catalpa {
	namespace {
		/**
		 * @brief 准备棋盘格地面材质。
		 *
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] unlit 已绑定着色器的标准无光照材质。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool PublishCheckerGroundMaterial(AssetManager& assets, const MaterialInstance& unlit,
		    std::string& error) {
			if (!unlit.definition.HasValue() || unlit.definition.Get()->shadingModel != MaterialShadingModel::Unlit) {
				error = "Checker ground requires a resolved Unlit material definition";
				return false;
			}
			const auto imageId = BuiltinAssetIds::CheckerGroundImage;
			const auto definitionId = BuiltinAssetIds::CheckerGroundDefinition;
			const auto materialId = BuiltinAssetIds::CheckerGround;

			/* 线性灰色的 2×2 棋盘；最后一个像素是平均色 mip，远处缩小时减少闪烁。 */
			ImageAsset image;
			image.sourceUri = "builtin:box-checker-ground";
			image.image.format = PixelFormat::RGBA8_UNorm;
			image.image.width = image.image.height = 2;
			image.image.mipLevels = 2;
			image.image.subresources = {{0, 0, 0, 2, 2, 1, 0, 8, 16, 16}, {1, 0, 0, 1, 1, 1, 16, 4, 4, 4}};
			for (const auto shade : std::array<unsigned char, 5>{48, 96, 96, 48, 72}) {
				image.image.pixels.insert(
				    image.image.pixels.end(), {std::byte{shade}, std::byte{shade}, std::byte{shade}, std::byte{255}});
			}
			if (!ValidateImageAsset(image, error))
				return false;

			const size_t imageBytes = sizeof(ImageAsset) + image.sourceUri.capacity() + 1 +
			    image.image.subresources.capacity() * sizeof(ImageSubresource) + image.image.pixels.capacity();
			if (!assets.PublishBuiltin<ImageAsset>(
			        imageId, 1, std::make_shared<const ImageAsset>(std::move(image)), imageBytes, error))
				return false;

			AssetVersionRef<ImageAsset> imageVersion;
			bool fallback = false;
			if (!assets.Acquire(imageId, imageVersion, fallback, error) || fallback) {
				if (error.empty())
					error = "Checker ground requires its exact image version";
				return false;
			}

			/* 样例地面沿用无光照基础色槽，自己的定义将棋盘设为默认贴图。 */
			MaterialDefinition definition = *unlit.definition.Get();
			auto texture = std::find_if(definition.parameters.begin(), definition.parameters.end(),
			    [](const auto& field) { return field.id == "baseColor_texture"; });
			if (texture == definition.parameters.end()) {
				error = "Checker ground requires the unlit base color map slot";
				return false;
			}
			texture->defaultValue = AssetHandle<ImageAsset>{imageId};
			texture->allowTextureFallback = false;
			if (!ValidateMaterialTexture(*texture, *imageVersion.Get(), error))
				return false;

			MaterialParameter uv;
			uv.id = "uv_scale_offset";
			uv.displayName = "UV scale / offset";
			uv.type = MaterialParameterType::Float4;
			/* 12m 平面上重复六次，每次包含两个格子，因此每格边长为 1m。 */
			uv.defaultValue = std::array<float, 4>{6, 6, 0, 0};
			definition.parameters.push_back(std::move(uv));

			MaterialParameter sampler;
			sampler.id = "baseColor_sampler";
			sampler.displayName = "Checker sampler";
			sampler.type = MaterialParameterType::UInt;
			/* Repeat + 缩小线性/mip 过滤；仅关闭放大线性过滤，保持近处格子的清晰边缘。 */
			sampler.defaultValue = DefaultMaterialSamplerFlags & ~(1u << 5);
			sampler.allowOverride = false;
			definition.parameters.push_back(std::move(sampler));
			if (!ValidateMaterialDefinition(definition, error))
				return false;

			const size_t definitionBytes = MaterialDefinitionBytes(definition);
			if (!assets.PublishBuiltin<MaterialDefinition>(definitionId, 1,
			        std::make_shared<const MaterialDefinition>(std::move(definition)), definitionBytes, error))
				return false;

			AssetVersionRef<MaterialDefinition> definitionVersion;
			if (!assets.Acquire(definitionId, definitionVersion, fallback, error) || fallback) {
				if (error.empty())
					error = "Checker ground requires its exact material definition";
				return false;
			}
			std::shared_ptr<const MaterialInstance> draft;
			if (!CreateMaterialInstance(definitionVersion, {}, {}, 1, draft, error))
				return false;

			/* 固定纹理版本，让材质、帧快照和 GPU 上传持有同一份像素，避免按逻辑 ID 追新。 */
			MaterialInstance resolved = *draft;
			resolved.textureVersions[0] = imageVersion;
			if (!ValidateMaterialInstance(resolved, error))
				return false;
			const size_t materialBytes = MaterialInstanceBytes(resolved);
			if (!assets.PublishBuiltin<MaterialInstance>(
			        materialId, 1, std::make_shared<const MaterialInstance>(std::move(resolved)), materialBytes, error))
				return false;
			return true;
		}
	}

	bool RegisterBuiltinSceneAssets(AssetManager& assets, std::string& error) try {
		using namespace BuiltinAssetIds;
		for (auto id : {CubeMesh, GroundMesh, SphereMesh, CapsuleMesh, CylinderMesh, PlaneMesh, QuadMesh}) {
			MeshData data;
			MeshResourceRef resource;
			auto status = CreateBuiltinPrimitiveMesh(id, data);
			if (status.IsSuccess())
				status = CreateMeshResource(data, resource);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			auto mesh = std::make_shared<const MeshAsset>(MeshAsset{resource, {"default"}});
			const auto& cpu = resource->GetData();
			if (id == GroundMesh) {
				/* 直接取示例平面的局部边界，碰撞面与可见地面共享高度和范围。 */
				const auto& a = cpu.localBounds.minimum;
				const auto& b = cpu.localBounds.maximum;
				auto floor = std::make_shared<CollisionShapeAsset>();
				floor->kind = CollisionShapeKind::TriangleMesh;
				floor->vertices = {{a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, a.y, a.z}, {a.x, a.y, a.z}};
				floor->indices = {0, 1, 2, 2, 3, 0};
				const size_t bytes = sizeof(CollisionShapeAsset) + floor->vertices.capacity() * sizeof(Math::Vec3) +
				    floor->indices.capacity() * sizeof(uint32_t);
				if (!assets.PublishBuiltin<CollisionShapeAsset>(GroundCollisionShape, 1, std::move(floor), bytes, error))
					return false;
			}
			const size_t resident = sizeof(MeshAsset) + sizeof(MeshResource) + cpu.vertexData.capacity() +
			    cpu.indexData.capacity() + cpu.attributes.capacity() * sizeof(VertexAttribute) +
			    cpu.submeshes.capacity() * sizeof(Submesh) + mesh->materialSlots.capacity() * sizeof(std::string) +
			    mesh->materialSlots.front().capacity() + 1;
			if (!assets.PublishBuiltin<MeshAsset>(id, 1, std::move(mesh), resident, error))
				return false;
		}
		/* 默认盒形边长一米，作者与运行时固定同一资源。 */
		if (!assets.PublishBuiltin<CollisionShapeAsset>(BoxCollisionShape, 1,
		        std::make_shared<const CollisionShapeAsset>(), sizeof(CollisionShapeAsset), error))
			return false;
		AssetVersionRef<MaterialInstance> unlit;
		bool fallback = false;
		return assets.Acquire(DefaultUnlitInstance, unlit, fallback, error) && !fallback &&
		    PublishCheckerGroundMaterial(assets, *unlit.Get(), error);
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool RegisterBuiltinAssets(AssetManager& manager, std::string& error) {
		try {
			/* 着色器就绪后才能建立内置材质。各着色模型固定自己的着色器版本，避免 PBR 定义误用 Unlit 字节码。 */
			AssetId shader, pbrShader;
			AssetVersionRef<ShaderAsset> shaderVersion, pbrShaderVersion;
			bool shaderFallback = false;
			if (!ShaderLibrary::BuiltinId("Unlit.slang", shader, error) ||
			    !manager.Acquire(shader, shaderVersion, shaderFallback, error) || shaderFallback ||
			    !ValidateShaderAsset(*shaderVersion.Get(), error)) {
				if (error.empty())
					error = "Builtin materials require the ready Unlit shader version";
				return false;
			}
			if (!ShaderLibrary::BuiltinId("Pbr.slang", pbrShader, error) ||
			    !manager.Acquire(pbrShader, pbrShaderVersion, shaderFallback, error) || shaderFallback ||
			    !ValidateShaderAsset(*pbrShaderVersion.Get(), error)) {
				if (error.empty())
					error = "Builtin PBR materials require the ready Pbr shader version";
				return false;
			}
			struct Pixel {
				AssetId id;
				std::array<uint8_t, 4> rgba;
				ImageSemantic semantic;
			};
			const std::array pixels{Pixel{BuiltinAssetIds::WhiteImage, {255, 255, 255, 255}, ImageSemantic::Color},
			    Pixel{BuiltinAssetIds::FlatNormalImage, {128, 128, 255, 255}, ImageSemantic::Normal},
			    Pixel{BuiltinAssetIds::ErrorImage, {255, 0, 255, 255}, ImageSemantic::Color}};
			for (const auto& pixel : pixels) {
				ImageAsset image;
				image.sourceUri = "builtin:mathematical-rgba8";
				image.image.format = PixelFormat::RGBA8_UNorm;
				image.image.width = image.image.height = 1;
				image.image.semantic = pixel.semantic;
				image.image.subresources.push_back({0, 0, 0, 1, 1, 1, 0, 4, 4, 4});
				for (auto byte : pixel.rgba)
					image.image.pixels.push_back(std::byte(byte));
				if (!ValidateImageAsset(image, error))
					return false;
				const size_t bytes = sizeof(ImageAsset) + image.sourceUri.capacity() + 1 +
				    image.image.subresources.capacity() * sizeof(ImageSubresource) + image.image.pixels.capacity();
				if (!manager.PublishBuiltin<ImageAsset>(
				        pixel.id, 1, std::make_shared<const ImageAsset>(std::move(image)), bytes, error))
					return false;
			}
			struct Material {
				BuiltinMaterialKind kind;
				AssetId definition, instance;
			};
			const std::array materials{Material{BuiltinMaterialKind::Unlit, BuiltinAssetIds::DefaultUnlitDefinition,
			                               BuiltinAssetIds::DefaultUnlitInstance},
			    Material{BuiltinMaterialKind::Pbr, BuiltinAssetIds::DefaultPbrDefinition,
			        BuiltinAssetIds::DefaultPbrInstance},
			    Material{BuiltinMaterialKind::Error, BuiltinAssetIds::ErrorDefinition, BuiltinAssetIds::ErrorInstance}};
			for (const auto& item : materials) {
				std::shared_ptr<const MaterialDefinition> definition;
				const bool pbr = item.kind == BuiltinMaterialKind::Pbr;
				if (!CreateBuiltinMaterialDefinition(item.kind, pbr ? pbrShader : shader, definition, error))
					return false;
				MaterialDefinition resolved = *definition;
				resolved.shaderVersion = pbr ? pbrShaderVersion : shaderVersion;
				if (!ValidateMaterialDefinition(resolved, error))
					return false;
				definition = std::make_shared<const MaterialDefinition>(std::move(resolved));
				const size_t definitionBytes = MaterialDefinitionBytes(*definition);
				if (!manager.PublishBuiltin<MaterialDefinition>(
				        item.definition, 1, std::move(definition), definitionBytes, error))
					return false;
				AssetVersionRef<MaterialDefinition> fixed;
				bool fallback = false;
				if (!manager.Acquire(item.definition, fixed, fallback, error) || fallback)
					return false;
				std::shared_ptr<const MaterialInstance> instance;
				if (!CreateMaterialInstance(fixed, {}, {}, 1, instance, error))
					return false;
				const size_t instanceBytes = MaterialInstanceBytes(*instance);
				if (!manager.PublishBuiltin<MaterialInstance>(
				        item.instance, 1, std::move(instance), instanceBytes, error))
					return false;
			}
			return manager.SetFallback<ImageAsset>(BuiltinAssetIds::ErrorImage, error) &&
			    manager.SetFallback<MaterialDefinition>(BuiltinAssetIds::ErrorDefinition, error) &&
			    manager.SetFallback<MaterialInstance>(BuiltinAssetIds::ErrorInstance, error);
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}
}
