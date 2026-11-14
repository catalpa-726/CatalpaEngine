#include "BoxGameplay.h"

#include "Assets/AssetManager.h"
#include "Runtime/RuntimeRegistration.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialValidation.h"
#include "Materials/MaterialAssetLoader.h"
#include "Materials/MaterialSurface.h"
#include "Images/ImageValidation.h"

#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Geometry/PrimitiveMesh.h"
#include "Geometry/MeshValidation.h"
#include "Core/Time/FrameTime.h"
#include "Core/Math/CoordinateConvention.h"
#include "Runtime/RuntimeServices.h"
#include "Runtime/Input/InputActions.h"
#include "ECS/SystemContext.h"
#include "Log/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <numbers>

namespace Catalpa {
	namespace {
		const PersistentEntityId ParentId = PersistentEntityId::FromWords(0x5330333100000001ULL, 1);
		const PersistentEntityId ChildId = PersistentEntityId::FromWords(0x5330333100000001ULL, 2);
		const PersistentEntityId CameraId = PersistentEntityId::FromWords(0x5330333200000001ULL, 1);
		const PersistentEntityId ForegroundId = PersistentEntityId::FromWords(0x5330333300000001ULL, 1);
		const PersistentEntityId GroundId = PersistentEntityId::FromWords(0x5330333300000001ULL, 2);

		/**
		 * @brief 准备棋盘格地面材质。
		 *
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] unlit 是否使用无光照材质。
		 * @param[out] output 接收 AssetVersionRef<MaterialInstance> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool PrepareCheckerGroundMaterial(AssetManager& assets, const MaterialInstance& unlit,
		    AssetVersionRef<MaterialInstance>& output, std::string& error) {
			if (!unlit.definition.HasValue() || unlit.definition.Get()->shadingModel != MaterialShadingModel::Unlit) {
				error = "Checker ground requires a resolved Unlit material definition";
				return false;
			}
			const auto imageId = AssetId::FromWords(0x434154414C504142ULL, 10);
			const auto definitionId = AssetId::FromWords(0x434154414C504142ULL, 11);
			const auto materialId = AssetId::FromWords(0x434154414C504142ULL, 12);

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

			/* 样例地面沿用内置 Unlit 的着色器版本，只增加纹理参数，保留共享材质定义。 */
			MaterialDefinition definition = *unlit.definition.Get();
			MaterialParameter texture;
			texture.id = "baseColor_texture";
			texture.displayName = "Checker texture";
			texture.type = MaterialParameterType::Texture2D;
			texture.defaultValue = AssetHandle<ImageAsset>{imageId};
			texture.textureSemantic = MaterialTextureSemantic::BaseColor;
			if (!ValidateMaterialTexture(texture, *imageVersion.Get(), error))
				return false;
			definition.parameters.push_back(std::move(texture));

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
			if (!assets.Acquire(materialId, output, fallback, error) || fallback) {
				if (error.empty())
					error = "Checker ground requires its exact material version";
				return false;
			}
			return true;
		}

		double Action(const ActionSnapshot* input, std::string_view id) {
			const auto* action = input ? input->Find(id) : nullptr;
			return action ? action->value : 0.0;
		}
	}

	struct BoxGameplay::State {
		Math::TransformTRS camera;
		double yaw = 0.0;
		double pitch = 0.0;
	};

	/**
	 * @brief 构造示例场景用于固定时间步相机控制的系统描述。
	 *
	 * @param[in] state 本次操作使用的状态。
	 *
	 * @return 可加入玩法调度器的相机控制系统描述，包含固定步更新与为独立运行场景克隆状态的回调。
	 */
	SystemDesc BoxGameplay::CameraSystem(std::shared_ptr<State> state) {
		SystemDesc gameplay;
		gameplay.id = "sandbox.camera-movement";
		gameplay.phase = SystemPhase::Gameplay;
		gameplay.accesses = {{"input.actions", SystemAccessMode::Read}, {"scene.commands", SystemAccessMode::Write}};
		gameplay.execute = [state](SystemContext& context) -> WorldStatus {
			const auto* input = context.services.input;
			/* 编辑导航不拥有游戏相机。没有玩法输入时也不回写缓存的旧 Transform。 */
			if (!input || !input->gameplayActive)
				return {};
			const double dx = Action(input, "look.yaw"), dy = Action(input, "look.pitch");
			state->yaw = std::remainder(state->yaw - dx * 0.0025, 2.0 * std::numbers::pi);
			/* 样例相机的明确俯仰范围，避免在极点翻转；鼠标位移不乘 dt 或 DPI。 */
			state->pitch =
			    std::clamp(state->pitch - dy * 0.0025, -std::numbers::pi / 2.0 + 0.01, std::numbers::pi / 2.0 - 0.01);
			const Math::Quat yaw{static_cast<float>(std::cos(state->yaw / 2.0)), 0.0f,
			    static_cast<float>(std::sin(state->yaw / 2.0)), 0.0f};
			const Math::Quat pitch{static_cast<float>(std::cos(state->pitch / 2.0)),
			    static_cast<float>(std::sin(state->pitch / 2.0)), 0.0f, 0.0f};
			state->camera.rotation = yaw * pitch;
			Math::Vec3 direction = (state->camera.rotation * Math::Vec3{1.0f, 0.0f, 0.0f}) *
			        static_cast<float>(Action(input, "move.right")) +
			    Math::Vec3{0.0f, 1.0f, 0.0f} * static_cast<float>(Action(input, "move.up")) +
			    (state->camera.rotation * Math::Vec3{0.0f, 0.0f, -1.0f}) *
			        static_cast<float>(Action(input, "move.forward"));
			const float distance = glm::length(direction);
			/* 限制样例的合成移动方向，避免同时按多轴时超过 3 m/s；不改原始输入快照。 */
			if (distance > 1.0f)
				direction /= distance;
			state->camera.translation += direction * static_cast<float>(3.0 * context.time.fixedDeltaSeconds);
			const EntityId camera = context.scene.Resolve(CameraId);
			if (!camera.HasValue())
				return {WorldError::InvalidEntity, "Sandbox camera no longer exists"};
			return context.scene.QueueSetLocal(camera, state->camera);
		};
		gameplay.cloneForPlay = [seed = *state](Scene& target) {
			auto independent = std::make_shared<State>(seed);
			TransformComponent pose;
			if (target.GetTransform(target.Resolve(CameraId), pose).IsSuccess()) {
				independent->camera = pose.local;
				const auto forward = pose.local.rotation * Math::Vec3{0, 0, -1};
				independent->yaw = std::atan2(-forward.x, -forward.z);
				independent->pitch = std::asin(std::clamp(double(forward.y), -1.0, 1.0));
			}
			return CameraSystem(std::move(independent));
		};
		return gameplay;
	}

	bool BoxGameplay::Attach(SceneManager& scenes, AssetManager& assets, bool reversedZ, bool errorMaterial) {
		try {
			m_Scenes = &scenes;
			m_ReportedHierarchy = false;
			m_State = std::make_shared<State>();
			m_State->camera.translation = Math::Vec3{0.0f, 2.0f, 10.0f};
			WorldStatus status = scenes.CreateScene("S03.2 Fixed-step Camera Sample", m_Scene);
			if (status.IsSuccess())
				status = scenes.SetActive(m_Scene);
			if (!status.IsSuccess()) {
				CAT_ERROR("Cannot install sandbox: {0}", status.message);
				Detach();
				return false;
			}
			Scene& scene = *scenes.GetScene(m_Scene);
			Math::TransformTRS parent;
			parent.translation = Math::Vec3{1.0f, 2.0f, 3.0f};
			const float halfAngle = std::numbers::pi_v<float> / 4.0f;
			parent.rotation = Math::Quat{std::cos(halfAngle), 0.0f, std::sin(halfAngle), 0.0f};
			parent.scale = Math::Vec3{2.0f, 3.0f, 4.0f};
			Math::TransformTRS child;
			child.translation = Math::Vec3{1.0f, 0.0f, 0.0f};
			status = scene.QueueCreate(ParentId, "Parent", parent);
			if (status.IsSuccess())
				status = scene.QueueCreate(ChildId, "Child", child);
			if (status.IsSuccess())
				status = scene.QueueCreate(CameraId, "Sandbox Camera", m_State->camera);
			Math::TransformTRS foreground;
			foreground.translation = {-2.3f, 0.4f, 5.0f};
			foreground.scale = {0.8f, 1.4f, 0.8f};
			Math::TransformTRS ground;
			ground.translation = {0.0f, -0.5f, 1.0f};
			if (status.IsSuccess())
				status = scene.QueueCreate(ForegroundId, "Foreground cube", foreground);
			if (status.IsSuccess())
				status = scene.QueueCreate(GroundId, "Ground plane", ground);
			/* Attach 发生在主循环外；此处是明确的启动结构安全点。 */
			if (status.IsSuccess())
				status = scene.CommitPending();
			if (status.IsSuccess())
				status = scene.QueueSetParent(scene.Resolve(ChildId), scene.Resolve(ParentId));
			CameraComponent camera;
			camera.lens.nearPlane = 0.1f;
			camera.lens.farPlane = 30.0f;
			camera.lens.reversedZ = reversedZ;
			if (status.IsSuccess())
				status = scene.QueueSetCamera(scene.Resolve(CameraId), camera);
			/* Box只生产示例内容；发布、版本和GPU派生分别交给引擎服务。 */
			std::string assetError;
			bool fallback = false;
			AssetVersionRef<MaterialInstance> material;
			if (!assets.Acquire(BuiltinMaterialId(BuiltinMaterialKind::Unlit), material, fallback, assetError)) {
				CAT_ERROR("Cannot acquire default material: {0}", assetError);
				Detach();
				return false;
			}
			const auto component = [&](const MeshResourceRef& mesh, AssetId id, const Math::Vec4& color,
			                           MeshRendererComponent& output) {
				const auto& data = mesh->GetData();
				auto value = std::make_shared<const MeshAsset>(MeshAsset{mesh, {"default"}});
				size_t resident = sizeof(MeshAsset) + sizeof(MeshResource) + data.vertexData.capacity() +
				    data.indexData.capacity() + data.attributes.capacity() * sizeof(VertexAttribute) +
				    data.submeshes.capacity() * sizeof(Submesh) + value->materialSlots.capacity() * sizeof(std::string);
				for (const auto& slot : value->materialSlots)
					resident += slot.capacity() + 1;
				if (!assets.PublishBuiltin<MeshAsset>(id, 1, std::move(value), resident, assetError))
					return false;
				AssetVersionRef<MeshAsset> version;
				if (!assets.Acquire(id, version, fallback, assetError))
					return false;
				/* 按字段赋值，避免新增 LOD 等可选字段后错位绑定材质。 */
				output = {};
				output.mesh = mesh;
				output.color = color;
				output.meshVersion = version;
				output.materials = {material};
				return true;
			};
			MeshData cube;
			MeshResourceRef geometry;
			if (status.IsSuccess()) {
				auto meshStatus = CreateCubeMesh(1.0f, cube);
				if (meshStatus.IsSuccess())
					meshStatus = CreateMeshResource(cube, geometry);
				if (!meshStatus.IsSuccess()) {
					CAT_ERROR("Cannot prepare sandbox geometry: {0}", meshStatus.message);
					Detach();
					return false;
				}
				MeshRendererComponent cubeRenderer;
				if (!component(geometry, AssetId::FromWords(0x434154414C504142ULL, 1),
				        Math::Vec4{0.1f, 0.8f, 0.5f, 1.0f}, cubeRenderer)) {
					CAT_ERROR("Cannot publish cube: {0}", assetError);
					Detach();
					return false;
				}
				status = scene.QueueSetMeshRenderer(scene.Resolve(ParentId), cubeRenderer);
				cubeRenderer.color = {0.9f, 0.4f, 0.2f, 1.0f};
				if (status.IsSuccess())
					status = scene.QueueSetMeshRenderer(scene.Resolve(ChildId), cubeRenderer);
				cubeRenderer.color = {0.55f, 0.25f, 0.85f, 1.0f};
				if (errorMaterial) {
					/* 故障样例创建一个引用缺失着色器的合法材质，渲染器负责决定回退行为。 */
					std::shared_ptr<const MaterialInstance> missing;
					const auto missingId = AssetId::FromWords(0x434154414C504142ULL, 3);
					if (!CreateBuiltinMaterialInstance(BuiltinMaterialKind::Unlit,
					        AssetId::FromWords(0x434154414C504142ULL, 4), 1,
					        AssetId::FromWords(0x434154414C504142ULL, 99), missing, assetError) ||
					    !assets.PublishBuiltin<MaterialInstance>(missingId, 1, missing,
					        MaterialInstanceBytes(*missing) + MaterialDefinitionBytes(*missing->definition.Get()),
					        assetError)) {
						CAT_ERROR("Cannot prepare error material sample: {0}", assetError);
						Detach();
						return false;
					}
					AssetVersionRef<MaterialInstance> fixed;
					if (!assets.Acquire(missingId, fixed, fallback, assetError)) {
						CAT_ERROR("{0}", assetError);
						Detach();
						return false;
					}
					cubeRenderer.materials = {fixed};
				}
				if (status.IsSuccess())
					status = scene.QueueSetMeshRenderer(scene.Resolve(ForegroundId), cubeRenderer);
				MeshData plane;
				MeshResourceRef planeGeometry;
				meshStatus = CreatePlaneMesh(12.0f, 12.0f, plane);
				if (meshStatus.IsSuccess())
					meshStatus = CreateMeshResource(plane, planeGeometry);
				if (!meshStatus.IsSuccess()) {
					CAT_ERROR("Cannot prepare ground geometry: {0}", meshStatus.message);
					Detach();
					return false;
				}
				MeshRendererComponent planeRenderer;
				if (!component(planeGeometry, AssetId::FromWords(0x434154414C504142ULL, 2),
				        Math::Vec4{1.0f, 1.0f, 1.0f, 1.0f}, planeRenderer)) {
					CAT_ERROR("Cannot publish plane: {0}", assetError);
					Detach();
					return false;
				}
				AssetVersionRef<MaterialInstance> groundMaterial;
				if (!PrepareCheckerGroundMaterial(assets, *material.Get(), groundMaterial, assetError)) {
					CAT_ERROR("Cannot prepare checker ground: {0}", assetError);
					Detach();
					return false;
				}
				planeRenderer.materials = {groundMaterial};
				if (status.IsSuccess())
					status = scene.QueueSetMeshRenderer(scene.Resolve(GroundId), planeRenderer);
			}
			if (status.IsSuccess())
				status = scene.CommitPending();
			if (!status.IsSuccess()) {
				CAT_ERROR("Cannot create sandbox content: {0}", status.message);
				Detach();
				return false;
			}

			auto gameplay = CameraSystem(m_State);
			status = m_Module.Install(scene, std::move(gameplay));
			if (!status.IsSuccess()) {
				CAT_ERROR("Cannot install sandbox gameplay: {0}", status.message);
				Detach();
				return false;
			}
			CAT_INFO(
			    "Navigate with the independent Scene Viewport camera. Use Play/Pause/Step/Stop for game simulation; F6/F7 remain standalone-runtime controls.");
			CAT_INFO("Scene sample: 4 instances share 2 meshes; near=0.1m, far=30m, reversed-Z={0}.", reversedZ);
			return true;
		} catch (const std::bad_alloc&) {
			CAT_ERROR("Cannot allocate sandbox gameplay state");
			Detach();
			return false;
		}
	}

	void BoxGameplay::ReportHierarchyOnce() {
		if (m_ReportedHierarchy || !m_Scenes || !m_Scene.HasValue())
			return;
		const Scene* scene = m_Scenes->GetScene(m_Scene);
		if (!scene)
			return;
		TransformComponent transform;
		const auto status = scene->GetTransform(scene->Resolve(ChildId), transform);
		if (!status.IsSuccess() || !transform.worldValid)
			return;

		const auto& position = transform.worldMatrix[3];
		CAT_INFO("[S03.1 sample] Child world origin = ({0}, {1}, {2}); expected (1, 2, 1).", position.x, position.y,
		    position.z);
		m_ReportedHierarchy = true;
	}

	void BoxGameplay::Detach() {
		if (m_Scenes && m_Scene.HasValue()) {
			if (Scene* scene = m_Scenes->GetScene(m_Scene)) {
				const auto stopped = m_Module.Uninstall(*scene);
				if (!stopped.IsSuccess())
					CAT_WARN("Gameplay registration remains until scene unload: {0}", stopped.message);
				if (const auto status = m_Scenes->Unload(m_Scene); !status.IsSuccess()) {
					CAT_CRITICAL("Cannot safely unload sandbox scene: {0}", status.message);
					std::abort();
				}
			}
		}
		m_Module.ForgetUnloadedScene();
		m_State.reset();
		m_Scenes = nullptr;
		m_Scene = {};
	}
}
