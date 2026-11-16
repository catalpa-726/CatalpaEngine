#include "catpch.h"
#include "Project/ProjectCreation.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "Scene/SceneSerializer.h"
#include "Core/Serialization/AuthorJson.h"
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	bool FindProjectFiles(const std::filesystem::path& directory, std::vector<std::filesystem::path>& candidates,
	    std::string& error) try {
		if (!directory.is_absolute() || !std::filesystem::is_directory(directory)) {
			error = "Choose an existing absolute project directory";
			return false;
		}
		std::vector<std::filesystem::path> found;
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().extension() == ".catalpaproj")
				found.push_back(std::filesystem::canonical(entry.path()));
			if (found.size() > 128) {
				error = "Project directory contains more than 128 project descriptions";
				return false;
			}
		}
		std::sort(found.begin(), found.end());
		candidates = std::move(found);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool CreateProject(const std::filesystem::path& parent, std::string_view name, std::filesystem::path& projectFile,
	    std::string& error) try {
		VirtualPath checked;
		if (name.empty() || name.size() > 120 || name.find_first_of("/\\") != std::string_view::npos ||
		    !VirtualPath::Parse("project:/" + std::string(name), checked, error)) {
			error = "Project name must be a valid single directory name (1-120 UTF-8 bytes): " + error;
			return false;
		}
		if (!parent.is_absolute() || !std::filesystem::is_directory(parent)) {
			error = "Choose an existing absolute parent directory";
			return false;
		}
		const auto base = std::filesystem::canonical(parent);
		const auto destination = base / std::filesystem::u8path(name);
		if (std::filesystem::exists(destination)) {
			error = "Project directory already exists; choose another name";
			return false;
		}
		StableId project, token;
		if (!GenerateStableId(project, error) || !GenerateStableId(token, error))
			return false;
		const auto sceneId = AssetBuildIdentity(project.ToString() + "/content/Scenes/Main.scene/scene");
		const auto stage = base / (".catalpa-new-" + token.ToString());
		if (!std::filesystem::create_directory(stage)) {
			error = "Cannot reserve a new project staging directory";
			return false;
		}
		/* 只清理由本次独占创建的暂存目录；提交后不再删除目标目录。 */
		struct StageOwner {
			std::filesystem::path path;
			~StageOwner() {
				if (!path.empty()) {
					std::error_code ignored;
					std::filesystem::remove_all(path, ignored);
				}
			}
		} owner{stage};
		std::filesystem::create_directories(stage / "Content" / "Scenes");
		SceneAsset scene;
		scene.name = "Main";
		SceneEntityAsset camera, light;
		if (!GeneratePersistentEntityId(camera.id, error) || !GeneratePersistentEntityId(light.id, error))
			return false;
		camera.name = "Main Camera";
		camera.camera.emplace();
		camera.local.translation = {0, 2, 6};
		light.name = "Directional Light";
		light.light.emplace();
		scene.entities = {camera, light};
		std::string sceneText;
		if (!SceneSerializer::Write(scene, sceneText, error))
			return false;
		using Json = AuthorJson::Json;
		const Json config{{"schemaVersion", 1}, {"id", project.ToString()}, {"name", name}, {"contentRoot", "Content"},
		    {"externalSourceRoots", Json::object()}, {"derivedDataRoot", "DerivedDataCache"}, {"cookedRoot", "Cooked"},
		    {"metadata", "asset_metadata.json"}, {"buildProfiles", "BuildProfiles.json"},
		    {"buildProfile", "desktop-vulkan"}, {"targetPlatform", "windows"}, {"buildConfiguration", "Debug"},
		    {"startupScene", sceneId.ToString()}, {"modules", Json::array()}};
		const Json profiles{{"schemaVersion", 1},
		    {"profiles",
		        {{"desktop-vulkan",
		            {{"platform", "windows"}, {"architecture", "x86_64"}, {"shaderTarget", "spirv"},
		                {"shaderProfile", "spirv_1_5"}, {"targetEnvironment", "vulkan1.3"},
		                {"assetPipeline", "static-model-v1"}}}}}};
		const Json metadata{{"schemaVersion", 1},
		    {"assets",
		        Json::array({{{"id", sceneId.ToString()}, {"type", "scene"}, {"path", "content:/Scenes/Main.scene"},
		            {"kind", "source"}, {"version", 1}}})}};
		const auto fileName = std::filesystem::u8path(std::string(name) + ".catalpaproj");
		if (!AtomicFileWriter::Write(stage / "Content" / "Scenes" / "Main.scene", sceneText, error) ||
		    !AtomicFileWriter::Write(stage / "Content" / "asset_metadata.json", metadata.dump(2), error) ||
		    !AtomicFileWriter::Write(stage / "BuildProfiles.json", profiles.dump(2), error) ||
		    !AtomicFileWriter::Write(stage / fileName, config.dump(2), error))
			return false;
		/* 加载校验完整候选，只有合法的项目才一次性发布到用户选定的目录。 */
		std::unique_ptr<ProjectSession> candidate;
		if (!LoadProject(stage / fileName, candidate, error))
			return false;
		candidate.reset();
		std::filesystem::rename(stage, destination);
		owner.path.clear();
		projectFile = destination / fileName;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
