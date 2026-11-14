#include "catpch.h"

#include "Geometry/MeshLod.h"
#include "Scene/DomainComponents.h"
#include "Scene/SceneSerializer.h"

#include "Scripting/ScriptValueArchive.h"
#include "Scripting/ScriptAsset.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Scene/PrefabAsset.h"
#include "Lighting/EnvironmentAsset.h"
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Core/Serialization/AuthorJson.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Camera/CameraProjection.h"
#include "Lighting/LightValidation.h"
#include "Lighting/EnvironmentSettings.h"
#include "Core/Serialization/SchemaMigrationRegistry.h"
#include "Core/PlatformServices.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa {
	namespace {
		using namespace AuthorJson;

		Json Values(const ScriptValues& values) {
			ArchiveValue archive;
			ArchiveError diagnostic;
			std::string text, error;
			if (!EncodeScriptValues(values, archive, error))
				throw std::invalid_argument(error);
			ArchiveLimits limits;
			limits.maxBytes = 16 * 1024 * 1024;
			limits.maxStringBytes = limits.maxBytes;
			if (!JsonArchive::Write(archive, text, diagnostic, limits))
				throw std::invalid_argument(diagnostic.ToString());
			return Json::parse(text);
		}

		ScriptValues ReadValues(const Json& value) {
			std::unique_ptr<ArchiveValue> archive;
			ArchiveError diagnostic;
			ArchiveLimits limits;
			limits.maxBytes = 16 * 1024 * 1024;
			limits.maxStringBytes = limits.maxBytes;
			if (!JsonArchive::Parse(value.dump(), archive, diagnostic, limits))
				throw std::invalid_argument(diagnostic.ToString());
			ScriptValues values;
			std::string error;
			if (!DecodeScriptValues(*archive, values, error))
				throw std::invalid_argument(error);
			return values;
		}

		std::string Hex(std::span<const std::byte> bytes) {
			static constexpr char digits[] = "0123456789abcdef";
			std::string result;
			result.reserve(bytes.size() * 2);
			for (auto b : bytes) {
				const auto n = std::to_integer<unsigned>(b);
				result += digits[n >> 4];
				result += digits[n & 15];
			}
			return result;
		}

		std::vector<std::byte> Unhex(const std::string& text) {
			if (text.size() % 2 || text.size() > 16 * 1024 * 1024)
				throw std::invalid_argument("Invalid inline procedural mesh length");
			const auto digit = [](char c) -> unsigned {
				if (c >= '0' && c <= '9')
					return unsigned(c - '0');
				if (c >= 'a' && c <= 'f')
					return unsigned(c - 'a' + 10);
				throw std::invalid_argument("Invalid inline procedural mesh encoding");
			};
			std::vector<std::byte> result(text.size() / 2);
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = std::byte((digit(text[2 * i]) << 4) | digit(text[2 * i + 1]));
			return result;
		}

		Json Camera(const CameraComponent& c) {
			const auto& l = c.lens;
			return {{"projection", uint32_t(l.projection)}, {"fov", l.verticalFovRadians}, {"near", l.nearPlane},
			    {"far", l.farPlane}, {"height", l.orthographicHeight}, {"infinite", l.infiniteFar},
			    {"reversed", l.reversedZ}, {"enabled", c.enabled}, {"priority", c.priority}, {"view", c.viewId},
			    {"history", c.historyRevision}};
		}

		/**
		 * @brief 从作者 JSON 读取相机镜头、启用状态、优先级和视图标识。
		 *
		 * @param[in] v 表示相机组件及镜头参数的 JSON 对象。
		 *
		 * @return 已验证镜头、优先级及视图身份的相机组件；字段、投影或标识不合法时抛出异常。
		 */
		CameraComponent ReadCamera(const Json& v) {
			Fields(v,
			    {"projection", "fov", "near", "far", "height", "infinite", "reversed", "enabled", "priority", "view",
			        "history"});
			CameraComponent c;
			const auto projection = Unsigned(v.at("projection"));
			if (projection > 1)
				throw std::invalid_argument("Unsupported projection");
			c.lens.projection = ProjectionType(projection);
			c.lens.verticalFovRadians = float(Number(v.at("fov")));
			c.lens.nearPlane = float(Number(v.at("near")));
			c.lens.farPlane = float(Number(v.at("far")));
			c.lens.orthographicHeight = float(Number(v.at("height")));
			c.lens.infiniteFar = v.at("infinite").get<bool>();
			c.lens.reversedZ = v.at("reversed").get<bool>();
			c.enabled = v.at("enabled").get<bool>();
			const auto& priorityValue = v.at("priority");
			if (!priorityValue.is_number_integer() ||
			    (priorityValue.is_number_unsigned() && priorityValue.get<uint64_t>() > INT32_MAX))
				throw std::invalid_argument("Camera priority exceeds int32 range");
			const auto priority = priorityValue.get<int64_t>();
			if (priority < INT32_MIN || priority > INT32_MAX)
				throw std::invalid_argument("Camera priority exceeds int32 range");
			c.priority = int32_t(priority);
			c.viewId = Unsigned(v.at("view"));
			c.historyRevision = Unsigned(v.at("history"));
			const auto status = ValidateCameraLens(c.lens);
			if (!status.IsSuccess() || !c.viewId || !c.historyRevision)
				throw std::invalid_argument("Invalid camera lens or view identity");
			return c;
		}

		Json Light(const LightComponent& c) {
			const auto& p = c.parameters;
			return {{"type", uint32_t(p.type)}, {"color", Array(p.color)}, {"intensity", p.intensity},
			    {"range", p.range}, {"inner", p.innerConeRadians}, {"outer", p.outerConeRadians},
			    {"shadows", p.castShadows}, {"bias", p.shadowBiasMeters}, {"enabled", c.enabled}};
		}

		LightComponent ReadLight(const Json& v) {
			Fields(v, {"type", "color", "intensity", "range", "inner", "outer", "shadows", "bias", "enabled"});
			LightComponent c;
			auto& p = c.parameters;
			const auto type = Unsigned(v.at("type"));
			if (type > 2)
				throw std::invalid_argument("Unsupported light type");
			p.type = LightType(type);
			p.color = Vec3(v.at("color"));
			p.intensity = float(Number(v.at("intensity")));
			p.range = float(Number(v.at("range")));
			p.innerConeRadians = float(Number(v.at("inner")));
			p.outerConeRadians = float(Number(v.at("outer")));
			p.castShadows = v.at("shadows").get<bool>();
			p.shadowBiasMeters = float(Number(v.at("bias")));
			c.enabled = v.at("enabled").get<bool>();
			std::string error;
			if (!ValidateLightParameters(p, error))
				throw std::invalid_argument(error);
			return c;
		}

		Json Environment(const EnvironmentComponent& c) {
			const auto& s = c.settings;
			return {{"asset", c.environment.id.ToString()}, {"intensity", s.intensity}, {"rotation", s.rotationRadians},
			    {"exposure", s.exposureEV}, {"background", s.drawBackground}, {"fog", s.fogEnabled},
			    {"fog_color", Array(s.fogColor)}, {"fog_density", s.fogDensityPerMeter}, {"enabled", c.enabled}};
		}

		EnvironmentComponent ReadEnvironment(const Json& v) {
			Fields(v,
			    {"asset", "intensity", "rotation", "exposure", "background", "fog", "fog_color", "fog_density",
			        "enabled"});
			EnvironmentComponent c;
			c.environment.id = Identity<AssetId>(v.at("asset"));
			auto& s = c.settings;
			s.intensity = float(Number(v.at("intensity")));
			s.rotationRadians = float(Number(v.at("rotation")));
			s.exposureEV = float(Number(v.at("exposure")));
			s.drawBackground = v.at("background").get<bool>();
			s.fogEnabled = v.at("fog").get<bool>();
			s.fogColor = Vec3(v.at("fog_color"));
			s.fogDensityPerMeter = float(Number(v.at("fog_density")));
			c.enabled = v.at("enabled").get<bool>();
			std::string error;
			if (!ValidateEnvironmentSettings(s, error))
				throw std::invalid_argument(error);
			return c;
		}

		bool InlineMesh(const SceneMeshAsset& source, AssetVersionRef<MeshAsset>& output, std::string& error) {
			auto mesh = std::make_shared<MeshAsset>();
			mesh->mesh = source.procedural;
			mesh->authorInline = true;
			for (size_t i = 0; i < source.materials.size(); ++i)
				mesh->materialSlots.push_back(std::to_string(i));
			std::vector<std::byte> bytes;
			std::array<std::byte, 32> digest;
			if (!GeometryAssetLoader::EncodeMesh(*mesh, bytes, error) || !ComputeSha256(bytes, digest, error))
				return false;
			StableId stable;
			for (size_t i = 0; i < stable.bytes.size(); ++i)
				stable.bytes[i] = std::to_integer<uint8_t>(digest[i]);
			AssetId id;
			return AssetId::TryParse(stable.ToString(), id) &&
			    AssetVersionRef<MeshAsset>::TryCreate(id, 1, mesh, output, error);
		}

		/**
		 * @brief 验证实体身份与父级关系，并确定可用于写出或实例化的实体顺序。
		 *
		 * @param[in] asset 作者场景资产。
		 * @param[out] ordered 接收排序后的实体索引。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Order(const SceneAsset& asset, std::vector<size_t>& ordered, std::string& error) {
			if (asset.entities.size() > 65536 || asset.name.size() > 4096) {
				error = "Scene author limits exceeded";
				return false;
			}
			std::map<PersistentEntityId, size_t> indices;
			for (size_t i = 0; i < asset.entities.size(); ++i)
				if (!asset.entities[i].id.HasValue() || !indices.emplace(asset.entities[i].id, i).second) {
					error = "Duplicate or invalid scene entity identity";
					return false;
				}
			std::vector<uint8_t> visited(asset.entities.size());
			for (size_t i = 0; i < asset.entities.size(); ++i) {
				std::vector<size_t> chain;
				size_t n = i;
				while (!visited[n]) {
					visited[n] = 1;
					chain.push_back(n);
					if (!asset.entities[n].parent.HasValue())
						break;
					const auto parent = indices.find(asset.entities[n].parent);
					if (parent == indices.end()) {
						error = "Scene parent reference is missing";
						return false;
					}
					n = parent->second;
				}
				if (visited[n] == 1 && (chain.empty() || chain.back() != n || asset.entities[n].parent.HasValue())) {
					error = "Scene hierarchy cycle";
					return false;
				}
				for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
					visited[*it] = 2;
					ordered.push_back(*it);
				}
			}
			return true;
		}
	}

	bool SceneSerializer::Capture(const Scene& scene, SceneAsset& output, std::string& error) try {
		if (!scene.CanEditNow()) {
			error = "Scene is busy";
			return false;
		}
		SceneAsset result;
		result.name = scene.GetName();
		const auto status = scene.GetWorld().ForEach([&](EntityId id) -> WorldStatus {
			SceneEntityAsset node;
			IdentityComponent identity;
			TransformComponent transform;
			RelationshipComponent relation;
			auto s = scene.GetIdentity(id, identity);
			if (s.IsSuccess())
				s = scene.GetTransform(id, transform);
			if (s.IsSuccess())
				s = scene.GetRelationship(id, relation);
			if (!s.IsSuccess())
				return s;
			node.id = identity.persistent;
			node.name = identity.name;
			node.local = transform.local;
			if (relation.parent.HasValue()) {
				IdentityComponent parent;
				if (auto p = scene.GetIdentity(relation.parent, parent); !p.IsSuccess())
					return p;
				node.parent = parent.persistent;
			}
			size_t count = 3;
			const auto read = [&]<class T>(std::optional<T>& into) {
				if (!scene.GetWorld().Has<T>(id))
					return WorldStatus{};
				++count;
				return scene.GetWorld().Get(id, into.emplace());
			};
			if (auto p = read(node.camera); !p.IsSuccess())
				return p;
			if (auto p = read(node.light); !p.IsSuccess())
				return p;
			if (auto p = read(node.environment); !p.IsSuccess())
				return p;
			if (auto p = read(node.script); !p.IsSuccess())
				return p;
			if (auto p = read(node.behavior); !p.IsSuccess())
				return p;
			if (auto p = read(node.prefab); !p.IsSuccess())
				return p;
			if (scene.GetWorld().Has<MeshRendererComponent>(id)) {
				++count;
				MeshRendererComponent renderer;
				if (auto p = scene.GetWorld().Get(id, renderer); !p.IsSuccess())
					return p;
				auto& mesh = node.mesh.emplace();
				if (renderer.meshVersion.HasValue() && !renderer.meshVersion.Get()->authorInline)
					mesh.mesh = renderer.meshVersion.GetId();
				mesh.procedural = mesh.mesh.HasValue() ? nullptr : renderer.mesh;
				mesh.pinned = renderer;
				mesh.color = renderer.color;
				mesh.visible = renderer.visible;
				for (const auto& material : renderer.materials)
					mesh.materials.push_back(material.GetId());
				for (const auto& lod : renderer.lods)
					mesh.lods.push_back(lod.GetId());
				mesh.lodErrors = renderer.lodErrors;
			}
			if (!CaptureDomainComponents(scene, id, node.dynamicComponents, error))
				return {WorldError::InvalidArgument, "Domain component capture failed"};
			if (!CollectDomainComponentVersions(scene.GetWorld(), id, node.dynamicDependencies, error))
				return {WorldError::InvalidArgument, "Cannot capture fixed domain versions"};
			count += node.dynamicComponents.size();
			size_t actual = 0;
			if (auto p = scene.GetWorld().CountComponents(id, actual); !p.IsSuccess())
				return p;
			if (count != actual)
				return {WorldError::InvalidArgument, "Component has no author serializer"};
			result.entities.push_back(std::move(node));
			return {};
		});
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		output = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool SceneSerializer::Write(const SceneAsset& asset, std::string& output, std::string& error) try {
		std::vector<size_t> order;
		if (!Order(asset, order, error))
			return false;
		Json doc{{"schemaVersion", 1}, {"name", asset.name}, {"entities", Json::array()}};
		for (auto index : order) {
			const auto& n = asset.entities[index];
			Json node{{"id", n.id.ToString()},
			    {"parent", n.parent.HasValue() ? Json(n.parent.ToString()) : Json(nullptr)}, {"name", n.name},
			    {"translation", Array(n.local.translation)}, {"scale", Array(n.local.scale)},
			    {"rotation",
			        Json::array({n.local.rotation.w, n.local.rotation.x, n.local.rotation.y, n.local.rotation.z})}};
			if (n.camera)
				node["camera"] = Camera(*n.camera);
			if (n.light)
				node["light"] = Light(*n.light);
			if (n.environment)
				node["environment"] = Environment(*n.environment);
			if (n.mesh) {
				const auto& m = *n.mesh;
				Json materialIds = Json::array();
				for (auto id : m.materials)
					materialIds.push_back(id.ToString());
				node["mesh"] = {{"asset", m.mesh.HasValue() ? Json(m.mesh.ToString()) : Json(nullptr)},
				    {"materials", materialIds}, {"visible", m.visible},
				    {"color", Json::array({m.color.x, m.color.y, m.color.z, m.color.w})}};
				if (!m.lods.empty()) {
					Json levels = Json::array();
					if (m.lods.size() != m.lodErrors.size())
						throw std::invalid_argument("Scene LOD metadata differs");
					for (size_t level = 0; level < m.lods.size(); ++level)
						levels.push_back({{"asset", m.lods[level].ToString()}, {"error", m.lodErrors[level]}});
					node["mesh"]["lods"] = std::move(levels);
				}
				if (!m.mesh.HasValue()) {
					if (m.materials.empty()) {
						error = "Inline geometry requires explicit material asset references before saving";
						return false;
					}
					MeshAsset procedural{m.procedural, {}};
					for (size_t i = 0; i < m.materials.size(); ++i)
						procedural.materialSlots.push_back(std::to_string(i));
					std::vector<std::byte> encoded;
					if (!GeometryAssetLoader::EncodeMesh(procedural, encoded, error))
						return false;
					node["mesh"]["procedural"] = Hex(encoded);
				}
			}
			if (n.script)
				node["script"] = {{"asset", n.script->script.ToString()}, {"enabled", n.script->enabled},
				    {"properties", Values(n.script->properties)}};
			if (n.behavior)
				node["behavior"] = {{"asset", n.behavior->graph.ToString()}, {"enabled", n.behavior->enabled},
				    {"variables", Values(n.behavior->variables)}};
			if (!n.dynamicComponents.empty()) {
				std::string archiveText;
				ArchiveError archiveError;
				if (!ValidateDomainComponents(n.dynamicComponents, error) ||
				    !JsonArchive::Write(ArchiveValue{n.dynamicComponents}, archiveText, archiveError))
					throw std::invalid_argument(error.empty() ? archiveError.ToString() : error);
				node["dynamicComponents"] = Json::parse(archiveText);
			}
			if (n.prefab) {
				Json mapping = Json::object();
				for (const auto& [local, instance] : n.prefab->entities)
					mapping[local.ToString()] = instance.ToString();
				node["prefab"] = {{"asset", n.prefab->source.ToString()}, {"version", n.prefab->sourceVersion},
				    {"entities", mapping}, {"overrides", Parse(n.prefab->overrides)}};
			}
			doc["entities"].push_back(std::move(node));
		}
		const auto text = doc.dump(2);
		SceneAsset checked;
		if (!Read(text, checked, error))
			return false;
		output = text;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool SceneSerializer::Read(std::string_view text, SceneAsset& output, std::string& error) try {
		const auto doc = Parse(text);
		Fields(doc, {"schemaVersion", "name", "entities"});
		if (Unsigned(doc.at("schemaVersion")) != 1)
			throw std::invalid_argument("Unsupported scene schema version");
		SceneAsset asset;
		asset.name = doc.at("name").get<std::string>();
		const auto& nodes = doc.at("entities");
		if (!nodes.is_array() || nodes.size() > 65536)
			throw std::invalid_argument("Invalid scene entity array");
		for (const auto& v : nodes) {
			Fields(v,
			    {"id", "parent", "name", "translation", "rotation", "scale", "camera", "mesh", "light", "environment",
			        "script", "behavior", "prefab", "dynamicComponents"});
			SceneEntityAsset n;
			n.id = Identity<PersistentEntityId>(v.at("id"));
			if (!v.at("parent").is_null())
				n.parent = Identity<PersistentEntityId>(v.at("parent"));
			n.name = v.at("name").get<std::string>();
			if (n.name.size() > 1024 || n.name.find('\0') != std::string::npos)
				throw std::invalid_argument("Entity name must be bounded text without NUL");
			n.local.translation = Vec3(v.at("translation"));
			n.local.scale = Vec3(v.at("scale"));
			const auto& r = v.at("rotation");
			if (!r.is_array() || r.size() != 4)
				throw std::invalid_argument("Expected w/x/y/z quaternion");
			n.local.rotation = {float(Number(r[0])), float(Number(r[1])), float(Number(r[2])), float(Number(r[3]))};
			if (!Math::ValidateTRS(n.local).IsSuccess())
				throw std::invalid_argument("Invalid entity TRS");
			if (v.contains("camera"))
				n.camera = ReadCamera(v.at("camera"));
			if (v.contains("light"))
				n.light = ReadLight(v.at("light"));
			if (v.contains("environment"))
				n.environment = ReadEnvironment(v.at("environment"));
			if (v.contains("mesh")) {
				const auto& m = v.at("mesh");
				Fields(m, {"asset", "materials", "color", "visible", "procedural", "lods"});
				auto& mesh = n.mesh.emplace();
				if (!m.at("asset").is_null())
					mesh.mesh = Identity<AssetId>(m.at("asset"));
				if (!m.at("materials").is_array() || m.at("materials").empty())
					throw std::invalid_argument("Mesh requires material references");
				for (const auto& id : m.at("materials"))
					mesh.materials.push_back(Identity<AssetId>(id));
				if (m.contains("lods")) {
					const auto& levels = m.at("lods");
					if (!levels.is_array() || levels.size() > 8)
						throw std::invalid_argument("Scene LOD budget");
					float previous = 0;
					std::set<AssetId> ids{mesh.mesh};
					for (const auto& level : levels) {
						Fields(level, {"asset", "error"});
						const auto id = Identity<AssetId>(level.at("asset"));
						const float value = float(Number(level.at("error")));
						if (!std::isfinite(value) || value <= previous || !ids.insert(id).second)
							throw std::invalid_argument("Scene LOD reference/error");
						previous = value;
						mesh.lods.push_back(id);
						mesh.lodErrors.push_back(value);
					}
				}
				const auto& c = m.at("color");
				if (!c.is_array() || c.size() != 4)
					throw std::invalid_argument("Mesh requires RGBA");
				mesh.color = {float(Number(c[0])), float(Number(c[1])), float(Number(c[2])), float(Number(c[3]))};
				if (mesh.color.x < 0 || mesh.color.y < 0 || mesh.color.z < 0 || mesh.color.w < 0 || mesh.color.w > 1)
					throw std::invalid_argument("Invalid linear color");
				mesh.visible = m.at("visible").get<bool>();
				if (m.contains("procedural")) {
					if (mesh.mesh.HasValue())
						throw std::invalid_argument("Mesh has both asset and procedural geometry");
					AssetDecodeContext context;
					context.maxResidentBytes = 64 * 1024 * 1024;
					AssetLoadResult decoded;
					if (!GeometryAssetLoader::DecodeMesh(
					        Unhex(m.at("procedural").get<std::string>()), context, decoded, error))
						return false;
					mesh.procedural = std::static_pointer_cast<const MeshAsset>(decoded.value)->mesh;
				}
				if (!mesh.mesh.HasValue() && !mesh.procedural)
					throw std::invalid_argument("Mesh source is missing");
			}
			if (v.contains("script")) {
				const auto& s = v.at("script");
				Fields(s, {"asset", "enabled", "properties"});
				n.script = ScriptComponent{
				    Identity<AssetId>(s.at("asset")), s.at("enabled").get<bool>(), ReadValues(s.at("properties"))};
			}
			if (v.contains("behavior")) {
				const auto& b = v.at("behavior");
				Fields(b, {"asset", "enabled", "variables"});
				n.behavior = BehaviorGraphComponent{
				    Identity<AssetId>(b.at("asset")), b.at("enabled").get<bool>(), ReadValues(b.at("variables"))};
				if (!ValidateBehaviorVariables(n.behavior->variables, error))
					return false;
			}
			if (v.contains("dynamicComponents")) {
				std::unique_ptr<ArchiveValue> values;
				ArchiveError diagnostic;
				if (!JsonArchive::Parse(v.at("dynamicComponents").dump(), values, diagnostic) ||
				    !std::holds_alternative<ArchiveValue::Object>(values->data))
					throw std::invalid_argument("Invalid dynamicComponents object");
				n.dynamicComponents = std::get<ArchiveValue::Object>(std::move(values->data));
				if (!ValidateDomainComponents(n.dynamicComponents, error))
					return false;
			}
			if (v.contains("prefab")) {
				const auto& p = v.at("prefab");
				Fields(p, {"asset", "version", "entities", "overrides"});
				auto& instance = n.prefab.emplace();
				instance.source = Identity<AssetId>(p.at("asset"));
				instance.sourceVersion = Unsigned(p.at("version"));
				if (!instance.sourceVersion || !p.at("entities").is_object() || !p.at("overrides").is_object())
					throw std::invalid_argument("Invalid prefab provenance");
				for (auto it = p.at("entities").begin(); it != p.at("entities").end(); ++it)
					instance.entities.emplace(
					    Identity<PersistentEntityId>(Json(it.key())), Identity<PersistentEntityId>(it.value()));
				std::set<PersistentEntityId> mapped;
				if (instance.entities.empty() || instance.entities.size() > 65536)
					throw std::invalid_argument("Prefab identity map is empty or oversized");
				for (const auto& [local, entity] : instance.entities)
					if (!mapped.insert(entity).second)
						throw std::invalid_argument("Prefab maps multiple local IDs to one entity");
				instance.overrides = p.at("overrides").dump();
			}
			asset.entities.push_back(std::move(n));
		}
		std::vector<size_t> order;
		if (!Order(asset, order, error))
			return false;
		output = std::move(asset);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	std::vector<AssetId> SceneSerializer::Dependencies(const SceneAsset& asset) {
		std::set<AssetId> ids;
		for (const auto& n : asset.entities) {
			if (n.mesh) {
				if (n.mesh->mesh.HasValue())
					ids.insert(n.mesh->mesh);
				ids.insert(n.mesh->materials.begin(), n.mesh->materials.end());
				ids.insert(n.mesh->lods.begin(), n.mesh->lods.end());
			}
			if (n.environment)
				ids.insert(n.environment->environment.id);
			if (n.script)
				ids.insert(n.script->script);
			if (n.behavior)
				ids.insert(n.behavior->graph);
			if (n.prefab)
				ids.insert(n.prefab->source);
			for (auto id : DomainComponentDependencies(n.dynamicComponents))
				ids.insert(id);
		}
		return {ids.begin(), ids.end()};
	}

	bool SceneSerializer::ReadMigrated(
	    std::string_view text, const SchemaMigrationRegistry& registry, SceneAsset& output, std::string& error) {
		std::unique_ptr<ArchiveValue> source;
		ArchiveError parse;
		ArchiveLimits limits;
		limits.maxBytes = 16 * 1024 * 1024;
		limits.maxStringBytes = limits.maxBytes;
		limits.maxNodes = 1000000;
		limits.maxCollectionEntries = 65536;
		ArchiveValue migrated;
		std::string canonical;
		if (!JsonArchive::Parse(text, source, parse, limits)) {
			error = parse.ToString();
			return false;
		}
		if (!registry.Migrate("scene", 1, *source, migrated, error))
			return false;
		if (!JsonArchive::Write(migrated, canonical, parse, limits)) {
			error = parse.ToString();
			return false;
		}
		return Read(canonical, output, error);
	}

	bool SceneSerializer::Instantiate(const SceneAsset& asset, SceneManager& scenes, AssetManager* assets,
	    SceneHandle& output, std::string& error) try {
		std::vector<size_t> order;
		if (!Order(asset, order, error))
			return false;
		/* 在创建场景前解析全部 Mesh/Material，缺依赖不会留下半成品场景。 */
		std::vector<std::vector<AssetDependencyValue>> domainVersions(asset.entities.size());
		for (size_t index = 0; index < asset.entities.size(); ++index) {
			const auto& node = asset.entities[index];
			domainVersions[index] = node.dynamicDependencies;
			for (const auto id : DomainComponentDependencies(node.dynamicComponents)) {
				const auto found = std::find_if(domainVersions[index].begin(), domainVersions[index].end(),
				    [&](const auto& value) { return value.id == id; });
				if (found != domainVersions[index].end())
					continue;
				AssetDependencyValue fixed;
				if (!assets || !assets->GetDependencySnapshot(id, fixed, error)) {
					if (error.empty())
						error = "Author domain dependency requires a ready AssetManager snapshot";
					return false;
				}
				domainVersions[index].push_back(std::move(fixed));
			}
		}
		std::set<PersistentEntityId> entityIds;
		for (const auto& node : asset.entities)
			entityIds.insert(node.id);
		const auto entityReferences = [&](const ScriptValues& values) {
			for (const auto& [name, value] : values)
				if (const auto* id = std::get_if<PersistentEntityId>(&value); id && !entityIds.contains(*id)) {
					error = "Missing scene entity reference in " + name + ": " + id->ToString();
					return false;
				}
			return true;
		};
		const auto available = [&]<class T>(AssetId id) {
			AssetVersionRef<T> fixed;
			bool fallback = false;
			if (assets && assets->GetStatus(id) == AssetStatus::Unloaded) {
				if (!assets->Request(id, error))
					return false;
				error = "Scene dependency is loading; retry after it becomes ready: " + id.ToString();
				return false;
			}
			if (!assets || !assets->Acquire(id, fixed, fallback, error) || fallback) {
				if (error.empty())
					error = "Required scene asset is unavailable: " + id.ToString();
				return false;
			}
			return true;
		};
		for (const auto& node : asset.entities) {
			if (node.environment && !available.template operator()<EnvironmentAsset>(node.environment->environment.id))
				return false;
			if (node.script &&
			    (!entityReferences(node.script->properties) ||
			        !available.template operator()<ScriptAsset>(node.script->script)))
				return false;
			if (node.behavior &&
			    (!entityReferences(node.behavior->variables) ||
			        !available.template operator()<BehaviorGraphAsset>(node.behavior->graph)))
				return false;
			if (node.prefab && !available.template operator()<PrefabAsset>(node.prefab->source))
				return false;
		}
		/* 在构造实体前准备网格渲染组件及其依赖，失败时仍可丢弃候选场景。 */
		std::map<size_t, MeshRendererComponent> renderers;
		for (auto index : order) {
			const auto& node = asset.entities[index];
			if (!node.mesh)
				continue;
			MeshRendererComponent renderer;
			if (!ResolveMesh(*node.mesh, assets, renderer, error))
				return false;
			renderers.emplace(index, std::move(renderer));
		}
		SceneHandle handle;
		const auto created = scenes.CreateScene(asset.name, handle);
		if (!created.IsSuccess()) {
			error = created.message;
			return false;
		}
		struct Candidate {
			SceneManager& scenes;
			SceneHandle handle;
			bool committed = false;

			~Candidate() {
				if (!committed)
					(void)scenes.Unload(handle);
			}
		} candidate{scenes, handle};
		auto& scene = *scenes.GetScene(handle);
		for (auto index : order) {
			const auto& n = asset.entities[index];
			auto status = scene.QueueCreate(n.id, n.name, n.local, scene.Resolve(n.parent));
			if (status.IsSuccess())
				status = scene.CommitPending();
			const auto id = scene.Resolve(n.id);
			if (status.IsSuccess() && n.camera)
				status = scene.QueueSetCamera(id, *n.camera);
			if (status.IsSuccess() && n.light)
				status = scene.QueueSetLight(id, *n.light);
			if (status.IsSuccess() && n.environment)
				status = scene.QueueSetEnvironment(id, *n.environment);
			if (status.IsSuccess() && n.script)
				status = scene.QueueSetScript(id, *n.script);
			if (status.IsSuccess() && n.behavior)
				status = scene.QueueSetBehaviorGraph(id, *n.behavior);
			if (status.IsSuccess() && n.prefab)
				status = scene.QueueSetPrefabInstance(id, *n.prefab);
			if (status.IsSuccess() && n.mesh)
				status = scene.QueueSetMeshRenderer(id, renderers.at(index));
			if (status.IsSuccess() &&
			    (!RestoreDomainComponents(scene, id, n.dynamicComponents, error) ||
			        !BindDomainComponentVersions(scene, id, domainVersions[index], error)))
				return false;
			if (status.IsSuccess())
				status = scene.CommitPending();
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		const auto status = TransformSystem::Update(scene);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		candidate.committed = true;
		output = handle;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool SceneSerializer::ResolveMesh(
	    const SceneMeshAsset& source, AssetManager* assets, MeshRendererComponent& output, std::string& error) try {
		MeshRendererComponent renderer;
		bool fallback = false;
		if (source.pinned)
			renderer = *source.pinned;
		else {
			if (source.mesh.HasValue()) {
				if (!assets || !assets->Acquire(source.mesh, renderer.meshVersion, fallback, error) || fallback)
					return false;
			} else if (source.procedural) {
				/* 内嵌几何的身份来自完整 payload；不向 AssetManager 注入虚构的磁盘 locator。 */
				if (!InlineMesh(source, renderer.meshVersion, error))
					return false;
			} else {
				error = "Mesh author source is missing";
				return false;
			}
			renderer.mesh = renderer.meshVersion.Get()->mesh;
			for (auto id : source.materials) {
				AssetVersionRef<MaterialInstance> material;
				if (!assets || !assets->Acquire(id, material, fallback, error) || fallback)
					return false;
				renderer.materials.push_back(std::move(material));
			}
		}
		if (renderer.lods.empty() && !source.lods.empty()) {
			if (source.lods.size() != source.lodErrors.size()) {
				error = "Scene LOD metadata differs";
				return false;
			}
			for (auto id : source.lods) {
				AssetVersionRef<MeshAsset> lod;
				if (!assets || !assets->Acquire(id, lod, fallback, error) || fallback)
					return false;
				renderer.lods.push_back(std::move(lod));
			}
			renderer.lodErrors = source.lodErrors;
		}
		renderer.color = source.color;
		renderer.visible = source.visible;
		if (!ValidateMeshResource(renderer.mesh).IsSuccess()) {
			error = "Invalid scene geometry";
			return false;
		}
		if (renderer.meshVersion.HasValue()) {
			if (!ValidateMeshAsset(*renderer.meshVersion.Get(), error) ||
			    renderer.meshVersion.Get()->mesh != renderer.mesh ||
			    renderer.materials.size() != renderer.meshVersion.Get()->materialSlots.size()) {
				if (error.empty())
					error = "Scene material count does not match geometry slots";
				return false;
			}
		} else if (!renderer.materials.empty()) {
			error = "Materials require versioned geometry";
			return false;
		}
		for (const auto& material : renderer.materials)
			if (!material.HasValue() || !ValidateMaterialInstance(*material.Get(), error))
				return false;
		if (!ValidateMeshLodChain(renderer.meshVersion, renderer.lods, renderer.lodErrors, error))
			return false;
		output = std::move(renderer);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool SceneSerializer::BindDependencies(
	    SceneAsset& asset, const AssetDecodeContext& context, std::string& error) try {
		auto candidate = asset;
		const auto fixed = [&]<class T>(AssetId id, AssetVersionRef<T>& output) {
			const auto dependency = std::find_if(context.dependencies.begin(), context.dependencies.end(),
			    [&](const auto& d) { return d.id == id && d.cppType == typeid(T); });
			if (dependency == context.dependencies.end()) {
				error = "Scene dependency is missing or has another type: " + id.ToString();
				return false;
			}
			return AssetVersionRef<T>::TryCreate(
			    id, dependency->version, std::static_pointer_cast<const T>(dependency->value), output, error);
		};
		for (auto& node : candidate.entities) {
			node.dynamicDependencies.clear();
			for (auto id : DomainComponentDependencies(node.dynamicComponents)) {
				const auto found = std::find_if(context.dependencies.begin(), context.dependencies.end(),
				    [&](const auto& value) { return value.id == id; });
				if (found == context.dependencies.end()) {
					error = "Missing fixed scene domain dependency: " + id.ToString();
					return false;
				}
				node.dynamicDependencies.push_back(*found);
			}
		}
		for (auto& node : candidate.entities)
			if (node.mesh) {
				MeshRendererComponent renderer;
				renderer.color = node.mesh->color;
				renderer.visible = node.mesh->visible;
				if (node.mesh->mesh.HasValue()) {
					if (!fixed(node.mesh->mesh, renderer.meshVersion))
						return false;
				} else if (!InlineMesh(*node.mesh, renderer.meshVersion, error))
					return false;
				renderer.mesh = renderer.meshVersion.Get()->mesh;
				for (auto id : node.mesh->materials) {
					AssetVersionRef<MaterialInstance> material;
					if (!fixed(id, material))
						return false;
					renderer.materials.push_back(std::move(material));
				}
				for (auto id : node.mesh->lods) {
					AssetVersionRef<MeshAsset> lod;
					if (!fixed(id, lod))
						return false;
					renderer.lods.push_back(std::move(lod));
				}
				renderer.lodErrors = node.mesh->lodErrors;
				node.mesh->pinned = std::move(renderer);
				MeshRendererComponent checked;
				if (!ResolveMesh(*node.mesh, nullptr, checked, error))
					return false;
			}
		asset = std::move(candidate);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
