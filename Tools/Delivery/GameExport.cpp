#include "catpch.h"
#include "Delivery/GameExport.h"

#include "AssetPipeline/AssetBuildCache.h"
#include "AssetPipeline/AssetCooker.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Assets/AssetManifest.h"
#include "Assets/PackageReader.h"
#include "Core/PlatformServices.h"
#include "Packager/PackageBuilder.h"
#include "Shaders/ShaderLibrary.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
	namespace {
		namespace fs = std::filesystem;
		using Json = nlohmann::json;

		/** @brief 读取有大小上限的构建索引，避免接受截断或过大的文件。 */
		std::vector<std::byte> ReadBytes(const fs::path& path, size_t limit) {
			const auto size = fs::file_size(path);
			if (size > limit)
				throw std::runtime_error("File exceeds export limit: " + AssetPathText(path));
			std::vector<std::byte> data(static_cast<size_t>(size));
			std::ifstream stream(path, std::ios::binary);
			if (!stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
				throw std::runtime_error("Cannot read: " + AssetPathText(path));
			return data;
		}
		/** @brief 解析受限的 JSON 文件。 */
		Json ReadJson(const fs::path& file) {
			auto data = ReadBytes(file, 4 * 1024 * 1024);
			return Json::parse(
			    reinterpret_cast<const char*>(data.data()), reinterpret_cast<const char*>(data.data()) + data.size());
		}
		/** @brief 写入临时交付目录，磁盘写满时不能报告发布成功。 */
		void Write(const fs::path& file, std::string_view value) {
			std::ofstream out;
			out.exceptions(std::ios::badbit | std::ios::failbit);
			out.open(file, std::ios::binary);
			out.write(value.data(), static_cast<std::streamsize>(value.size()));
			out.close();
		}
		/** @brief 仅删除本次独占创建的发布暂存目录，目标目录由重命名原子发布。 */
		struct Stage {
			fs::path root;
			explicit Stage(const fs::path& parent) {
				for (size_t i = 0; i < 32; ++i) {
					auto candidate = parent /
					    (".catalpa-export-" +
					        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
					        std::to_string(i));
					if (fs::create_directory(candidate)) {
						root = fs::canonical(candidate);
						return;
					}
				}
				throw std::runtime_error("Cannot allocate a unique export staging directory");
			}
			~Stage() {
				std::error_code ec;
				fs::remove_all(root, ec);
			}
		};
		/** @brief 检查取消并发布一次阶段变化。 */
		void Phase(const GameExportRequest& request, const std::string& label) {
			if (request.cancellation.IsCancellationRequested())
				throw std::runtime_error("Export cancelled");
			if (request.progress)
				request.progress(label);
		}
		/** @brief 按旧清单中的来源标识重建引用资产；无旧清单时按受控根寻找依赖。 */
		AssetBuildResult CookEntry(
		    const GameExportRequest& request, const ProjectConfig& original, const ProjectConfig& staging) {
			const auto shaders = request.toolResources / "Shaders";
			std::map<AssetId, fs::path> sources, scriptSources;
			auto importers = AssetImportRegistry::Defaults();
			auto scan = [&](const fs::path& root, const std::string& name) {
				for (const auto& f : fs::recursive_directory_iterator(root)) {
					if (sources.size() >= 65536)
						throw std::runtime_error("Export source index exceeds 65536 assets");
					if (request.cancellation.IsCancellationRequested())
						throw std::runtime_error("Export cancelled");
					if (f.is_symlink() || !f.is_regular_file() || !importers.Find(f.path()))
						continue;
					auto file = fs::canonical(f.path());
					if (!IsPathWithinRoot(file, root))
						throw std::runtime_error("Source redirects outside its root: " + AssetPathText(file));
					auto logical = name + "/" + AssetPathText(file.lexically_relative(root));
					const auto identity = original.id.ToString() + "/" + logical + "/";
					sources.emplace(AssetBuildIdentity(identity + "request"), file);
					/* 首次导出即可定位具体脚本，并将编译器的行号与诊断返回给作者。 */
					const auto ext = AssetSourceExtension(file);
					if (ext == ".cs" || ext == ".lua" || ext == ".script")
						scriptSources.emplace(AssetBuildIdentity(identity + "script"), file);
				}
			};
			scan(original.contentRoot, "content");
			for (const auto& [name, root] : original.externalSourceRoots)
				scan(root, "external/" + name);

			AssetRegistry previous;
			auto oldManifest = original.cookedRoot / "Assets.manifest";
			std::string error;
			if (fs::exists(oldManifest) &&
			    !AssetManifest::Read(ReadBytes(oldManifest, 64 * 1024 * 1024), previous, error))
				throw std::runtime_error(error);
			auto entry = fs::canonical(request.entrySource);
			std::set<fs::path> done, visiting;
			std::map<fs::path, AssetId> owners;
			for (const auto& [id, file] : sources)
				owners.emplace(file, id);
			auto build = [&](const fs::path& source) {
				Phase(request, "Cook: " + AssetPathText(source.filename()));
				AssetBuildRequest job;
				job.project = staging;
				job.source = source;
				job.shaderRoot = shaders;
				job.cancellation = request.cancellation;
				if (!AssetBuildCache::ReadSettings(original, owners.at(source), job.settings, error))
					throw std::runtime_error(error);
				/* 全新项目也能仅凭原图重建场景引用的各个颜色空间视图。 */
				if (TextureImporter::SupportsMaterialMap(source))
					job.settings.materialTextureViews = true;
				return AssetCooker::Build(std::move(job));
			};
			if (!owners.contains(entry) || AssetSourceExtension(entry) != ".scene")
				throw std::runtime_error(
				    "Choose a saved .scene or .scene.json inside the current project source roots");
			std::function<AssetBuildResult(const fs::path&)> cook = [&](const fs::path& file) -> AssetBuildResult {
				if (visiting.size() >= 128 || !visiting.insert(file).second)
					throw std::runtime_error("Circular source dependency: " + AssetPathText(file));
				auto result = build(file);
				while (!result.success && result.missingDependency.HasValue()) {
					const auto missing = result.missingDependency;
					fs::path dependency;
					if (const auto script = scriptSources.find(missing); script != scriptSources.end())
						dependency = script->second;
					else if (auto* record = previous.Find(missing)) {
						auto path = fs::u8path(record->locator.path.RelativePath());
						auto part = path.begin();
						AssetId owner;
						if (part != path.end() && *part == "objects" && ++part != path.end() &&
						    AssetId::TryParse(AssetPathText(*part), owner) && sources.contains(owner))
							dependency = sources.at(owner);
					}
					if (!dependency.empty() && !done.contains(dependency)) {
						auto child = cook(dependency);
						if (!child.success)
							return child;
					} else {
						// 首次导出没有来源索引时，仅在隔离目录尝试未构建的源，按依赖进展重试。
						bool progress = false;
						for (const auto& [owner, candidate] : sources) {
							if (done.contains(candidate) || visiting.contains(candidate))
								continue;
							auto candidateResult = build(candidate);
							if (candidateResult.success) {
								done.insert(candidate);
								progress = true;
								if (std::any_of(candidateResult.records.begin(), candidateResult.records.end(),
								        [&](const auto& row) { return row.id == missing; }))
									break;
							}
						}
						if (!progress)
							return result;
					}
					result = build(file);
				}
				visiting.erase(file);
				if (result.success)
					done.insert(file);
				return result;
			};
			return cook(entry);
		}

		/** @brief 按模板清单部署已验证的宿主文件，禁止绝对路径和源根逃逸。 */
		void CopyRuntime(const GameExportRequest& request, const fs::path& game, Json& deployed) {
			auto runtime = fs::canonical(request.runtimeTemplate);
			const auto description = ReadJson(runtime / "runtime-template.json");
			if (description.at("schemaVersion") != 1 || description.at("runtimeOnly") != true)
				throw std::runtime_error("Invalid runtime-only template: " + AssetPathText(runtime));
			std::set<std::string> paths;
			for (const auto& item : description.at("files")) {
				auto name = item.get<std::string>();
				auto relative = fs::u8path(name);
				if (relative.empty() || relative.has_root_path() || name.find(':') != std::string::npos ||
				    relative.lexically_normal() != relative || name.find("..") != std::string::npos ||
				    name == "Game.catpkg" || name == "run.cmd" || name == "deployment.json" ||
				    relative.begin()->wstring() == L"Shaders" || name.starts_with("Resources/Shaders/"))
					throw std::runtime_error("Invalid template file name: " + name);
				const auto from = fs::canonical(runtime / relative);
				if (!IsPathWithinRoot(from, runtime) || !fs::is_regular_file(from))
					throw std::runtime_error("Missing or escaped template file: " + AssetPathText(from));
				if (request.cancellation.IsCancellationRequested())
					throw std::runtime_error("Export cancelled");
				fs::create_directories((game / relative).parent_path());
				fs::copy_file(from, game / relative);
				if (AssetBuildDigest(ReadBytes(game / relative, 128 * 1024 * 1024)) !=
				    description.at("sha256").at(name).get<std::string>())
					throw std::runtime_error("Runtime template hash mismatch: " + name);
				if (relative.extension() == L".json" && !ReadJson(game / relative).is_object())
					throw std::runtime_error("Invalid runtime configuration: " + name);
				paths.insert(name);
				if (name != "CatalpaBox.exe")
					deployed.push_back(name);
			}
			for (const auto* required :
			    {"CatalpaBox.exe", "Catalpa.dll", "ThirdPartyNotices.md", "Resources/Managed/Catalpa.Scripting.dll",
			        "Resources/Environment/Overcast.catasset",
			        "Resources/Config/InputProfiles.json", "Resources/Config/RendererProfiles.json",
			        "Resources/Config/AudioDefaults.json", "Resources/Config/PhysicsDefaults.json",
			        "Resources/Config/UIRuntimeDefaults.json", "Resources/Localization/en-US.json",
			        "Resources/Localization/zh-CN.json", "Resources/Fonts/Inter/InterVariable.ttf",
			        "Resources/Fonts/Inter/Inter-Regular.ttf", "Resources/Fonts/Inter/LICENSE.txt",
			        "Resources/Fonts/NotoSansSC/NotoSansSC-Regular.otf", "Resources/Fonts/NotoSansSC/LICENSE.txt"})
				if (!paths.contains(required))
					throw std::runtime_error(std::string("Runtime template is missing: ") + required);
		}
	}

	bool ExportWindowsGame(const GameExportRequest& request, std::string& error) try {
		namespace fs = std::filesystem;
		Phase(request, "Checking project and runtime template");
		if (!request.output.is_absolute() || request.output.filename().empty() || fs::exists(request.output))
			throw std::runtime_error("Select a new output directory; existing directories are preserved");
		if (!fs::is_regular_file(request.runtimeTemplate / "runtime-template.json"))
			throw std::runtime_error(
			    "Runtime template missing: " + AssetPathText(request.runtimeTemplate / "runtime-template.json"));
		std::unique_ptr<ProjectSession> session;
		if (!LoadProject(request.projectFile, session, error))
			return false;
		const auto parent = fs::canonical(request.output.parent_path());
		std::vector<fs::path> protectedRoots{session->config.contentRoot, session->config.cookedRoot,
		    session->config.derivedDataRoot, request.toolResources, request.runtimeTemplate};
		for (const auto& [name, path] : session->config.externalSourceRoots)
			protectedRoots.push_back(path);
		for (const auto& root : protectedRoots)
			if (IsPathWithinRoot(parent, fs::weakly_canonical(root)))
				throw std::runtime_error("Export overlaps a source, template or build directory");
		Stage stage(parent);
		auto game = stage.root / "Game";
		fs::create_directories(game);
		auto config = session->config;
		config.projectFile = stage.root / "Release.catalpaproj";
		config.cookedRoot = stage.root / "Cooked";
		config.derivedDataRoot = stage.root / "Cache";
		config.startupScene.reset();
		if (!SaveProjectConfig(config, error))
			return false;
		auto built = CookEntry(request, session->config, config);
		if (!built.success) {
			error = built.error;
			return false;
		}
		Phase(request, "Checking scene and dependencies");
		AssetRegistry records;
		VirtualFileSystem files;
		if (!records.Replace(built.publishedCatalog, error))
			return false;
		std::set<std::string> mounts;
		for (const auto& r : records.GetRecords())
			if (mounts.insert(r.locator.path.MountName()).second &&
			    !files.MountReadOnly(r.locator.path.MountName(), config.cookedRoot, 0, error))
				return false;
		if (!PackageBuilder::Build(records, files, built.primary, game / "Game.catpkg", request.cancellation, error))
			return false;
		Phase(request, "Deploying runtime files");
		Json deployed = Json::array();
		CopyRuntime(request, game, deployed);
		const auto source = request.toolResources / "Shaders";
		const auto dest = game / "Resources/Shaders";
		std::vector<AssetRecord> shaders;
		if (!ShaderLibrary::ReadBuiltinCatalog(source, "shaders", shaders, error))
			return false;
		for (const auto& record : shaders) {
			auto file = fs::u8path(record.locator.path.RelativePath());
			fs::create_directories((dest / file).parent_path());
			fs::copy_file(source / file, dest / file);
			deployed.push_back("Resources/Shaders/" + AssetPathText(file));
		}
		fs::copy_file(source / "BuiltinShaders.manifest", dest / "BuiltinShaders.manifest");
		deployed.push_back("Resources/Shaders/BuiltinShaders.manifest");
		if (!ShaderLibrary::ReadBuiltinCatalog(dest, "shaders", shaders, error))
			return false;
		std::shared_ptr<const PackageReader> package;
		if (!PackageReader::Open(game / "Game.catpkg", package, error))
			return false;
		for (const auto& shader : shaders) {
			const auto* reference = package->Catalog().Find(shader.id);
			if (reference && (reference->version != shader.version || reference->contentHash != shader.contentHash))
				throw std::runtime_error("Shader changed during export: " + shader.id.ToString());
		}
		Write(game / "run.cmd", "@echo off\r\n\"%~dp0CatalpaBox.exe\" --run-package \"%~dp0Game.catpkg\"\r\n");
		Write(game / "deployment.json",
		    Json{{"schemaVersion", 1}, {"package", "Game.catpkg"}, {"files", deployed}}.dump(2));
		Phase(request, "Publishing validated directory");
		fs::rename(game, request.output);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	ToolCommandResult RunGameExport(std::span<const std::string> args, CancellationToken cancellation) {
		GameExportRequest request;
		request.cancellation = cancellation;
		ToolCommandResult result;
		std::map<std::string, fs::path*> values{{"--project", &request.projectFile}, {"--scene", &request.entrySource},
		    {"--output", &request.output}, {"--resources", &request.toolResources},
		    {"--template", &request.runtimeTemplate}};
		if (args.size() != 11) {
			result.exitCode = 2;
			result.diagnostics.push_back(
			    "--export-game --project <file> --scene <file> --output <new-dir> --resources <tool-resources> --template <runtime-template>");
			return result;
		}
		for (size_t i = 1; i < args.size(); i += 2) {
			const auto it = values.find(args[i]);
			if (it == values.end() || !it->second->empty()) {
				result.exitCode = 2;
				result.diagnostics.push_back("Invalid or duplicate option: " + args[i]);
				return result;
			}
			*it->second = fs::u8path(args[i + 1]);
		}
		std::string error;
		request.progress = [&](std::string value) { result.diagnostics.push_back(std::move(value)); };
		if (ExportWindowsGame(request, error)) {
			result.outputs.push_back(request.output);
			result.exitCode = 0;
		} else
			result.diagnostics.push_back(error);
		return result;
	}
}
