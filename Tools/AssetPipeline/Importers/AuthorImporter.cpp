#include "AssetPipeline/Importers/AuthorImporter.h"

#include "AssetPipeline/AssetImportRegistry.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabAsset.h"
#include "Scripting/ScriptAsset.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "MaterialGraph/MaterialGraphCompiler.h"
#include "Core/Serialization/AuthorJson.h"
#include "Shaders/ShaderLibrary.h"
#include "Materials/MaterialValidation.h"
#include "BehaviorGraph/BehaviorGraphCompiler.h"

namespace Catalpa::Tools {
	bool AuthorImporter::Import(AssetBuildContext& context, AssetImportResult& output, std::string& error) const try {
		std::vector<std::byte> bytes;
		if (!context.Read(context.request.source, bytes, error) || !context.ReserveWorking(bytes.size() * 8, error))
			return false;
		const std::string input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		std::string canonical;
		std::vector<AssetId> dependencies;
		AssetImportResult result;
		const auto append = [&](std::string key, std::string type, std::span<const std::byte> data,
		                        std::vector<AssetId> refs) {
			const auto id = context.Identity(key);
			std::sort(refs.begin(), refs.end());
			refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
			result.assets.push_back(
			    {id, key, key, ImportedAuthorProduct{type, {data.begin(), data.end()}, std::move(refs)}});
			result.mappings.push_back({id, key, key, type});
			return id;
		};
		if (m_Type == "scene") {
			SceneAsset scene;
			if (!SceneSerializer::Read(input, scene, error) || !SceneSerializer::Write(scene, canonical, error))
				return false;
			dependencies = SceneSerializer::Dependencies(scene);
		} else if (m_Type == "prefab") {
			PrefabAsset prefab;
			if (!ReadPrefab(input, prefab, error) || !WritePrefab(prefab, canonical, error))
				return false;
			dependencies = SceneSerializer::Dependencies(prefab.scene);
			for (const auto& nest : prefab.nested)
				dependencies.push_back(nest.asset);
		} else if (m_Type == "script") {
			ScriptAsset script;
			if (AssetSourceExtension(context.request.source) == ".lua")
				script.source = input;
			else if (!ReadScriptAsset(input, script, error))
				return false;
			if (!LuaScriptBackend::CheckSource(script.source, error) || !WriteScriptAsset(script, canonical, error))
				return false;
			for (const auto& [name, id] : script.modules)
				dependencies.push_back(id);
		} else if (m_Type == "behavior_graph") {
			BehaviorGraphAsset graph;
			if (!BehaviorGraphCompiler::Compile(input, graph, error) || !WriteBehaviorGraph(graph, canonical, error))
				return false;
		} else if (m_Type == "material") {
			const auto doc = AuthorJson::Parse(input);
			const auto kind = doc.at("kind").get<std::string>();
			if (kind == "definition") {
				std::shared_ptr<const MaterialDefinition> value;
				if (!ParseMaterialDefinition(input, value, error))
					return false;
				dependencies.push_back(value->shader.id);
				for (const auto& parameter : value->parameters)
					if (const auto* image = std::get_if<AssetHandle<ImageAsset>>(&parameter.defaultValue);
					    image && image->HasValue())
						dependencies.push_back(image->id);
				result.primary = append("material_definition", "material_definition", bytes, std::move(dependencies));
			} else if (kind == "instance") {
				const auto definition = AuthorJson::Identity<AssetId>(doc.at("definitionId"));
				if (!AuthorJson::Unsigned(doc.at("definitionVersion"))) {
					error = "Author material needs a fixed definition version";
					return false;
				}
				dependencies.push_back(definition);
				result.primary = append("material_instance", "material_instance", bytes, std::move(dependencies));
			} else {
				error = "Unknown author material kind";
				return false;
			}
			output = std::move(result);
			return true;
		} else if (m_Type == "material_graph") {
			MaterialGraphSource graph;
			if (!ReadMaterialGraph(input, graph, error))
				return false;
			const auto* sdk = _wgetenv(L"VULKAN_SDK");
			if (!sdk) {
				error = "Material graph Cook requires VULKAN_SDK";
				return false;
			}
			const auto shaderId = context.Identity("graph/shader");
			MaterialGraphCompileResult compiled;
			MaterialGraphCompileRequest request{graph, context.request.shaderRoot.parent_path() / "ShaderSources", sdk,
			    context.request.project.derivedDataRoot / "MaterialGraphs", shaderId, context.request.cancellation};
			if (!MaterialGraphCompiler::Compile(request, compiled, error) ||
			    !MaterialGraphCompiler::Definition(graph, shaderId, canonical, error))
				return false;
			if (!ShaderLibrary::EncodePayload(*compiled.shader, bytes, error))
				return false;
			append("graph/shader", "shader", bytes, {});
			auto textures = MaterialGraphCompiler::Textures(graph);
			textures.push_back(shaderId);
			const auto definitionId = append("graph/definition", "material_definition",
			    std::as_bytes(std::span(canonical.data(), canonical.size())), textures);
			/* 材质定义的内容版本由烘焙顺序确定，实例的 JSON 在编码时填入该版本。 */
			canonical =
			    AuthorJson::Json{{"schemaVersion", 1}, {"kind", "instance"}, {"definitionId", definitionId.ToString()},
			        {"definitionVersion", 0}, {"revision", 1}, {"parameters", AuthorJson::Json::object()}}
			        .dump();
			result.primary = append("material_instance", "material_instance",
			    std::as_bytes(std::span(canonical.data(), canonical.size())), {definitionId});
			output = std::move(result);
			return true;
		} else {
			error = "Unknown author importer domain";
			return false;
		}
		result.primary = append(
		    m_Type, m_Type, std::as_bytes(std::span(canonical.data(), canonical.size())), std::move(dependencies));
		output = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
