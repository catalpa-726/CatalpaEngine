#include "catpch.h"

#include "Animation/AnimationAssetLoader.h"
#include "Physics/CollisionAssetLoader.h"
#include "Navigation/NavMeshAssetLoader.h"
#include "Audio/AudioAssetLoader.h"
#include "UI/UIDocumentAsset.h"
#include "Particles/ParticleEffect.h"
#include "Terrain/TerrainAsset.h"
#include "UI/FontAssetLoader.h"
#include "Runtime/RuntimeRegistration.h"

#include "ECS/ComponentRegistry.h"
#include "Core/Math/TransformMath.h"
#include "Assets/BuiltinAssetIds.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Images/ImageAssetLoader.h"
#include "Materials/MaterialAssetLoader.h"
#include "Shaders/ShaderLibrary.h"
#include "Lighting/EnvironmentAssetLoader.h"
#include "Lighting/LightValidation.h"
#include "Lighting/EnvironmentSettings.h"
#include "Camera/CameraProjection.h"
#include "Scene/SceneAssetLoader.h"
#include "Scene/PrefabAsset.h"
#include "Scripting/ScriptAsset.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Scripting/ScriptValueArchive.h"

namespace Catalpa {
	namespace {
		float FieldNumber(const ArchiveValue& value) {
			if (const auto* v = std::get_if<double>(&value.data))
				return static_cast<float>(*v);
			if (const auto* v = std::get_if<int64_t>(&value.data))
				return static_cast<float>(*v);
			return static_cast<float>(std::get<uint64_t>(value.data));
		}
		/**
		 * @brief 按 TRS 数学约定校验归档中的变换组件。
		 * 将组件模式约束转换为数学对象约束。按 w、x、y、z 的顺序读取四元数，保留输入值树。
		 *
		 * @param[in] value 场景中待验证的变换组件归档字段。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 变换字段可读且通过 TRS 校验时为 true，否则为 false。
		 */
		bool TransformSchema(const ArchiveValue& value, std::string& error) {
			const auto& translation = std::get<ArchiveValue::Array>(value.Find("translation")->data);
			const auto& rotation = std::get<ArchiveValue::Array>(value.Find("rotation")->data);
			const auto& scale = std::get<ArchiveValue::Array>(value.Find("scale")->data);
			Math::TransformTRS transform;
			transform.translation = {
			    FieldNumber(translation[0]), FieldNumber(translation[1]), FieldNumber(translation[2])};
			transform.rotation = {
			    FieldNumber(rotation[0]), FieldNumber(rotation[1]), FieldNumber(rotation[2]), FieldNumber(rotation[3])};
			transform.scale = {FieldNumber(scale[0]), FieldNumber(scale[1]), FieldNumber(scale[2])};
			const auto status = Math::ValidateTRS(transform);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			return true;
		}
		/**
		 * @brief 校验作者场景中的网格渲染器字段及其资源标识。
		 * 与场景使用相同的颜色约定。模式只校验作者引用，实际网格槽数在资源依赖解析后检查。
		 *
		 * @param[in] value 场景中待验证的网格渲染器组件归档字段。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 颜色及作者引用等字段合法时为 true，否则为 false。
		 */
		bool MeshRendererSchema(const ArchiveValue& value, std::string& error) {
			const auto& materials = std::get<ArchiveValue::Array>(value.Find("materials")->data);
			const auto& color = std::get<ArchiveValue::Array>(value.Find("color")->data);
			if (materials.empty()) {
				error = "Mesh renderer requires material asset references";
				return false;
			}
			for (size_t channel = 0; channel < 4; ++channel) {
				const float component = FieldNumber(color[channel]);
				if (component < 0 || (channel == 3 && component > 1)) {
					error = "Mesh renderer requires nonnegative linear RGB and alpha in [0,1]";
					return false;
				}
			}
			return true;
		}
	}
	/**
	 * @brief 按光源类型及其单位约定校验归档的光照参数。
	 * 光源强度沿用该类型定义的单位。模式校验保留作者值，GPU 常量由后续步骤生成。
	 *
	 * @param[in] value 场景中待验证的灯光组件归档字段。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 光源参数满足对应类型的要求时为 true，否则为 false。
	 */
	static bool LightSchema(const ArchiveValue& value, std::string& error) {
		LightParameters light;
		light.type = static_cast<LightType>(static_cast<uint32_t>(FieldNumber(*value.Find("type"))));
		const auto& color = std::get<ArchiveValue::Array>(value.Find("color")->data);
		light.color = {FieldNumber(color[0]), FieldNumber(color[1]), FieldNumber(color[2])};
		light.intensity = FieldNumber(*value.Find("intensity"));
		light.range = FieldNumber(*value.Find("range"));
		light.innerConeRadians = FieldNumber(*value.Find("inner_cone"));
		light.outerConeRadians = FieldNumber(*value.Find("outer_cone"));
		light.castShadows = std::get<bool>(value.Find("cast_shadows")->data);
		light.shadowBiasMeters = FieldNumber(*value.Find("shadow_bias"));
		return ValidateLightParameters(light, error);
	}

	static bool EnvironmentSchema(const ArchiveValue& value, std::string& error) {
		const float intensity = FieldNumber(*value.Find("intensity"));
		const float rotation = FieldNumber(*value.Find("rotation"));
		const float exposure = FieldNumber(*value.Find("exposure_ev"));
		EnvironmentSettings settings{.intensity = intensity, .rotationRadians = rotation, .exposureEV = exposure};
		if (const auto* enabled = value.Find("fog_enabled"))
			settings.fogEnabled = std::get<bool>(enabled->data);
		if (const auto* density = value.Find("fog_density"))
			settings.fogDensityPerMeter = FieldNumber(*density);
		if (const auto* color = value.Find("fog_color")) {
			const auto& values = std::get<ArchiveValue::Array>(color->data);
			settings.fogColor = {FieldNumber(values[0]), FieldNumber(values[1]), FieldNumber(values[2])};
		}
		return ValidateEnvironmentSettings(settings, error);
	}

	static bool CameraSchema(const ArchiveValue& value, std::string& error) {
		CameraLens lens;
		lens.projection = static_cast<ProjectionType>(static_cast<uint32_t>(FieldNumber(*value.Find("projection"))));
		lens.verticalFovRadians = FieldNumber(*value.Find("vertical_fov"));
		lens.nearPlane = FieldNumber(*value.Find("near_plane"));
		lens.farPlane = FieldNumber(*value.Find("far_plane"));
		lens.orthographicHeight = FieldNumber(*value.Find("orthographic_height"));
		lens.infiniteFar = std::get<bool>(value.Find("infinite_far")->data);
		lens.reversedZ = std::get<bool>(value.Find("reversed_z")->data);
		const auto status = ValidateCameraLens(lens);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		error.clear();
		return true;
	}

	bool RegisterRuntimeComponentSchemas(ComponentRegistry& registry, std::string& error) {
		try {
			std::vector<ComponentSchema> schemas;
			const auto properties = [](const ArchiveValue& data, std::string& error) {
				ScriptValues values;
				const auto* fields = data.Find("properties");
				return fields && DecodeScriptValues(*fields, values, error);
			};
			schemas.push_back({"catalpa.script", 1,
			    {{"asset", "Script asset", ComponentFieldKind::StableId, true},
			        {"enabled", "Enabled", ComponentFieldKind::Bool, true},
			        {"properties", "Properties", ComponentFieldKind::Object, true}},
			    properties});
			schemas.push_back({"catalpa.behavior_graph", 1,
			    {{"asset", "Behavior graph", ComponentFieldKind::StableId, true},
			        {"enabled", "Enabled", ComponentFieldKind::Bool, true},
			        {"variables", "Initial variables", ComponentFieldKind::Object, true}},
			    [](const ArchiveValue& data, std::string& error) {
				    ScriptValues values;
				    const auto* fields = data.Find("variables");
				    if (!fields || !DecodeScriptValues(*fields, values, error))
					    return false;
				    return ValidateBehaviorVariables(values, error);
			    }});
			schemas.push_back({"catalpa.prefab_instance", 1,
			    {{"asset", "Prefab source", ComponentFieldKind::StableId, true},
			        {"version", "Source version", ComponentFieldKind::UInt, true, 1.0},
			        {"entities", "Local to instance map", ComponentFieldKind::Object, true},
			        {"overrides", "Baseline and overrides", ComponentFieldKind::Object, true}},
			    [](const ArchiveValue& data, std::string& error) {
				    const auto* field = data.Find("entities");
				    const auto* map = field ? std::get_if<ArchiveValue::Object>(&field->data) : nullptr;
				    std::set<StableId> instances;
				    if (!map || map->empty()) {
					    error = "Prefab entity mapping is empty";
					    return false;
				    }
				    for (const auto& [local, value] : *map) {
					    const auto* text = std::get_if<std::string>(&value.data);
					    StableId a, b;
					    if (!StableId::TryParse(local, a) || !text || !StableId::TryParse(*text, b) ||
					        !instances.insert(b).second) {
						    error = "Prefab entity mapping is invalid or aliases another local entity";
						    return false;
					    }
				    }
				    return true;
			    }});
			schemas.push_back({"catalpa.identity", 1,
			    {{"persistent_id", "Persistent ID", ComponentFieldKind::StableId, true},
			        {"name", "Name", ComponentFieldKind::String, true}},
			    {}});
			schemas.push_back({"catalpa.transform", 1,
			    {{"translation", "Translation (m)", ComponentFieldKind::Float3, true},
			        {"rotation", "Rotation (w/x/y/z)", ComponentFieldKind::Float4, true},
			        {"scale", "Scale", ComponentFieldKind::Float3, true}},
			    TransformSchema});
			schemas.push_back({"catalpa.relationship", 1,
			    {{"parent", "Parent persistent ID", ComponentFieldKind::StableId, false}}, {}});
			/* 只描述持久标识和作者字段。运行时解析的网格及材质版本引用和世界矩阵不按指针序列化。 */
			schemas.push_back({"catalpa.mesh_renderer", 1,
			    {{"mesh", "Mesh asset ID", ComponentFieldKind::StableId, true},
			        {"materials", "Material asset IDs", ComponentFieldKind::StableIdArray, true},
			        {"color", "Instance color", ComponentFieldKind::Float4, true},
			        {"visible", "Visible", ComponentFieldKind::Bool, true}},
			    MeshRendererSchema});
			schemas.push_back({"catalpa.light", 1,
			    {{"type", "Light type", ComponentFieldKind::UInt, true, 0.0, 2.0},
			        {"color", "Linear RGB", ComponentFieldKind::Float3, true},
			        {"intensity", "Illuminance (lux) / intensity (cd)", ComponentFieldKind::Number, true},
			        {"range", "Range (m)", ComponentFieldKind::Number, true},
			        {"inner_cone", "Inner cone (rad)", ComponentFieldKind::Number, true},
			        {"outer_cone", "Outer cone (rad)", ComponentFieldKind::Number, true},
			        {"cast_shadows", "Cast shadows", ComponentFieldKind::Bool, true},
			        {"shadow_bias", "Shadow bias (m)", ComponentFieldKind::Number, true},
			        {"enabled", "Enabled", ComponentFieldKind::Bool, true}},
			    LightSchema});
			schemas.push_back({"catalpa.environment", 1,
			    {{"environment", "Environment asset", ComponentFieldKind::StableId, true},
			        {"intensity", "Intensity", ComponentFieldKind::Number, true},
			        {"rotation", "Rotation (rad)", ComponentFieldKind::Number, true},
			        {"exposure_ev", "Exposure (EV)", ComponentFieldKind::Number, true},
			        {"draw_background", "Draw background", ComponentFieldKind::Bool, true},
			        {"enabled", "Enabled", ComponentFieldKind::Bool, true},
			        {"fog_enabled", "Uniform fog", ComponentFieldKind::Bool, false},
			        {"fog_color", "Linear fog RGB", ComponentFieldKind::Float3, false},
			        {"fog_density", "Fog density (1/m)", ComponentFieldKind::Number, false}},
			    EnvironmentSchema});
			schemas.push_back({"catalpa.camera", 1,
			    {{"projection", "Projection", ComponentFieldKind::UInt, true, 0.0, 1.0},
			        {"vertical_fov", "Vertical FOV (rad)", ComponentFieldKind::Number, true},
			        {"near_plane", "Near plane (m)", ComponentFieldKind::Number, true},
			        {"far_plane", "Far plane (m)", ComponentFieldKind::Number, true},
			        {"orthographic_height", "Orthographic height (m)", ComponentFieldKind::Number, true},
			        {"infinite_far", "Infinite far", ComponentFieldKind::Bool, true},
			        {"reversed_z", "Reversed Z", ComponentFieldKind::Bool, true},
			        {"enabled", "Enabled", ComponentFieldKind::Bool, true},
			        {"priority", "Priority", ComponentFieldKind::Int, true, -2147483648.0, 2147483647.0},
			        {"view_id", "View ID", ComponentFieldKind::UInt, true, 1.0},
			        {"history_revision", "History revision", ComponentFieldKind::UInt, true, 1.0}},
			    CameraSchema});
			return registry.RegisterMany(schemas, error);
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool RegisterRuntimeAssetLoaders(AssetManager& manager, std::string& error) {
		/* 只供新建manager的启动组合使用；失败即由宿主丢弃未发布manager，不伪称已注册全部领域。 */
		return FontAssetLoader::Register(manager, error) && ShaderLibrary::RegisterAssetLoader(manager, error) &&
		    RegisterGeometryAssetLoaders(manager, error) && RegisterImageAssetLoader(manager, error) &&
		    RegisterMaterialAssetLoaders(manager, error) && RegisterEnvironmentAssetLoader(manager, error) &&
		    RegisterSceneAssetLoader(manager, error) && RegisterPrefabAssetLoader(manager, error) &&
		    RegisterScriptAssetLoader(manager, error) && RegisterBehaviorGraphAssetLoader(manager, error) &&
		    RegisterAnimationAssetLoaders(manager, error) && RegisterPhysicsAssetLoaders(manager, error) &&
		    RegisterNavigationAssetLoader(manager, error) && AudioAssetLoader::Register(manager, error) &&
		    UIDocumentLoader::Register(manager, error) && RegisterParticleLoader(manager, error) &&
		    RegisterTerrainAssetLoader(manager, error);
	}

	AssetId BuiltinMaterialId(BuiltinMaterialKind kind, bool definition) noexcept {
		switch (kind) {
		case BuiltinMaterialKind::Unlit:
			return definition ? BuiltinAssetIds::DefaultUnlitDefinition : BuiltinAssetIds::DefaultUnlitInstance;
		case BuiltinMaterialKind::Pbr:
			return definition ? BuiltinAssetIds::DefaultPbrDefinition : BuiltinAssetIds::DefaultPbrInstance;
		case BuiltinMaterialKind::Error:
			return definition ? BuiltinAssetIds::ErrorDefinition : BuiltinAssetIds::ErrorInstance;
		default:
			return {};
		}
	}
}
