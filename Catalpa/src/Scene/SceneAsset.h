#pragma once
#include "Core/Serialization/Archive.h"
#include "Assets/AssetLoadResult.h"
#include "Scene/Components/IdentityComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Scene/Components/ScriptComponent.h"
#include "Scene/Components/BehaviorGraphComponent.h"
#include "Scene/Components/PrefabInstanceComponent.h"

namespace Catalpa {
	struct SceneMeshAsset {
		AssetId mesh;
		std::vector<AssetId> materials;
		std::vector<AssetId> lods;
		std::vector<float> lodErrors;
		MeshResourceRef procedural;
		Math::Vec4 color{1};
		bool visible = true;
		/* 内存内克隆保活已经显示的版本；写文件时只保存稳定身份。 */
		std::optional<MeshRendererComponent> pinned;
	};

	struct SceneEntityAsset {
		PersistentEntityId id, parent;
		std::string name;
		Math::TransformTRS local;
		std::optional<CameraComponent> camera;
		std::optional<SceneMeshAsset> mesh;
		std::optional<LightComponent> light;
		std::optional<EnvironmentComponent> environment;
		std::optional<ScriptComponent> script;
		std::optional<BehaviorGraphComponent> behavior;
		std::optional<PrefabInstanceComponent> prefab;
		ArchiveValue::Object dynamicComponents;
		std::vector<AssetDependencyValue> dynamicDependencies;
	};

	/* 作者值和稳定引用，不含 World 槽位、系统回调或 GPU 对象。 */
	struct SceneAsset {
		std::string name;
		std::vector<SceneEntityAsset> entities;
	};
	/**
	 * @brief 估计场景资产自身容器及归档值树的驻留载荷。
	 * 容量与值树节点的保守载荷估算；不重复计入独立资产版本拥有的像素/几何。
	 *
	 * @param[in] scene 待估算驻留载荷的场景资产，包含实体和组件归档数据。
	 *
	 * @return 保守估算的字节数，不重复计算独立图像和几何资产持有的载荷。
	 */
	CATALPA_API size_t EstimateSceneAssetResidentBytes(const SceneAsset&) noexcept;
}
