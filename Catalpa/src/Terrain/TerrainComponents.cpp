#include "catpch.h"

#include "Terrain/TerrainService.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Core/Serialization/AuthorJson.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
	const ComponentType& ComponentTraits<TerrainComponent>::Get() {
		return Detail::MakeComponentType<TerrainComponent>();
	}
	namespace {
		ArchiveValue Encode(const TerrainComponent& v) {
			const auto text = AuthorJson::Json{{"asset", v.asset.id.ToString()}, {"viewDistance", v.viewDistance},
			    {"lodDistance", v.lodDistance}, {"enabled", v.enabled}, {"collisionEnabled", v.collisionEnabled}}
			                      .dump();
			std::unique_ptr<ArchiveValue> result;
			ArchiveError error;
			if (!JsonArchive::Parse(text, result, error))
				throw std::invalid_argument(error.ToString());
			return std::move(*result);
		}

		TerrainComponent Decode(const ArchiveValue& value) {
			std::string text;
			ArchiveError error;
			if (!JsonArchive::Write(value, text, error))
				throw std::invalid_argument(error.ToString());
			const auto j = AuthorJson::Parse(text);
			AuthorJson::Fields(j, {"asset", "viewDistance", "lodDistance", "enabled", "collisionEnabled"});
			TerrainComponent v;
			v.asset.id = AuthorJson::Identity<AssetId>(j.at("asset"));
			v.viewDistance = float(AuthorJson::Number(j.at("viewDistance")));
			v.lodDistance = float(AuthorJson::Number(j.at("lodDistance")));
			v.enabled = j.at("enabled").get<bool>();
			v.collisionEnabled = j.at("collisionEnabled").get<bool>();
			if (v.viewDistance < 1 || v.viewDistance > 100000 || v.lodDistance < 1 || v.lodDistance > v.viewDistance)
				throw std::invalid_argument("Terrain view/LOD distance must satisfy 1m <= LOD <= view <= 100km");
			return v;
		}
	} /* 命名空间 */
	WorldStatus SetTerrain(Scene& scene, EntityId id, const TerrainComponent& value) {
		try {
			Decode(Encode(value));
		} catch (...) {
			return {WorldError::InvalidArgument, "Invalid terrain component"};
		}
		return QueueDynamicComponent(scene, id,
		    [id, value](World& w) { return w.Has<TerrainComponent>(id) ? w.Set(id, value) : w.Add(id, value); });
	}

	bool CaptureTerrainComponents(
	    const Scene& scene, EntityId id, DynamicComponentMap& output, std::string& error) try {
		if (scene.GetWorld().Has<TerrainComponent>(id)) {
			TerrainComponent value;
			const auto status = scene.GetWorld().Get(id, value);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			output["terrain"] = Encode(value);
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool ValidateTerrainComponents(const DynamicComponentMap& values, std::string& error) try {
		if (auto i = values.find("terrain"); i != values.end())
			Decode(i->second);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool RestoreTerrainComponents(
	    Scene& scene, EntityId id, const DynamicComponentMap& values, std::string& error) try {
		if (auto i = values.find("terrain"); i != values.end()) {
			const auto status = SetTerrain(scene, id, Decode(i->second));
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool RestoreTerrainComponents(
	    World& world, EntityId id, const DynamicComponentMap& values, std::string& error) try {
		if (auto i = values.find("terrain"); i != values.end()) {
			const auto value = Decode(i->second);
			const auto status = world.Has<TerrainComponent>(id) ? world.Set(id, value) : world.Add(id, value);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	std::vector<AssetId> TerrainComponentDependencies(const DynamicComponentMap& values) {
		if (auto i = values.find("terrain"); i != values.end())
			return {Decode(i->second).asset.id};
		return {};
	}
} /* 命名空间 Catalpa */
