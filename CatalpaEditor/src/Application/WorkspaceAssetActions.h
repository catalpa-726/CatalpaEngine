#pragma once

#include "Scene/Scene.h"

namespace Catalpa::Editor {
	class BuildPanel;
	enum class AuthorAssetAction { None, BindScript, BindGraph, BindMaterial, BindComponent, BindTexture, PlaceModel, PlacePrefab };
	/** @brief 构建完成前保存发起操作的项目、文档和实体代次，避免结果送入后来打开的场景。 */
	struct WorkspaceAssetActions {
		BuildPanel* build = nullptr;
		std::filesystem::path shaders, source;
		StableId project, document;
		EntityReference entity;
		uint64_t generation = 0;
		size_t materialSlot = 0;
		AssetVersionRef<MeshAsset> materialMesh;
		AssetVersionRef<MaterialInstance> previousMaterial;
		std::string componentType, textureParameter, textureKey;
		AssetHandle<ImageAsset> previousTexture;
		AssetId previousComponentAsset, readyComponentAsset;
		Math::Vec3 position{};
		AuthorAssetAction action = AuthorAssetAction::None;
		struct Source {
			std::filesystem::path path;
			std::string label;
			AssetId id;
			std::string_view type;
		};
		std::vector<Source> sources;
		StableId playAfterBuild;
		std::vector<std::filesystem::path> playScripts;
		uint64_t playBuildGeneration = 0;
		std::set<std::filesystem::path> pendingMaterialBuilds;
		std::array<char, 192> filter{};
		bool indexed = false;
	};
}
