#include "AssetPipeline/ProjectSourceImport.h"
#include "AssetPipeline/AssetBuildCache.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "Core/PlatformServices.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa::Tools {
	namespace fs = std::filesystem;

	/**
	 * @brief 校验已有的项目内图片，命中时恢复其导入路径及已烘焙的材质通道。
	 * @param[in,out] request 复用成功后改为指向项目副本的构建请求。
	 * @param[in] directory 已占用的导入目录。
	 * @param[in] original 本次导入器实际读到的源图片、长度和哈希。
	 * @param[out] reused 已有副本与原件内容一致并可复用时为 true。
	 * @param[out] error 已有副本或构建配方读取失败的原因。
	 * @return 完成检查时为 true，即使内容不同、需要另选目录也是如此。
	 */
	static bool ReuseTexture(AssetBuildRequest& request, const fs::path& directory,
	    const AssetSourceDependency& original, bool& reused, std::string& error) {
		reused = false;
		const auto copy = directory / original.path.filename();
		if (!fs::is_directory(directory) || !fs::is_regular_file(copy) || fs::canonical(copy) != copy ||
		    fs::file_size(copy) != original.bytes)
			return true;
		auto candidate = request;
		candidate.source = copy;
		AssetBuildContext existing(std::move(candidate));
		std::vector<std::byte> bytes;
		if (!existing.Initialize(error) || !existing.Read(copy, bytes, error))
			return false;
		if (existing.Dependencies().front().sha256 != original.sha256)
			return true;
		AssetImportSettings saved;
		if (!AssetBuildCache::ReadSettings(request.project, existing.Identity("request"), saved, error))
			return false;
		request.settings.materialTextureViews |= saved.materialTextureViews;
		request.source = copy;
		reused = true;
		return true;
	}

	/** @brief 用同一路径解析、依赖跟踪和目标校验完成审阅或提交。 */
	static bool ProcessSource(AssetBuildRequest& request, const fs::path& directory, ProjectImportReview* review,
	    const ProjectImportReview* approved, std::string& error) try {
		const auto source = fs::canonical(request.source);
		const auto content = fs::canonical(request.project.contentRoot);
		const auto dest = fs::canonical(directory);
		if (!fs::is_regular_file(source) || !fs::is_directory(dest) || !IsPathWithinRoot(dest, content)) {
			error = "Choose a regular source file and a destination directory inside project Content";
			return false;
		}
		request.source = source;
		bool mounted = IsPathWithinRoot(source, content);
		for (const auto& [name, root] : request.project.externalSourceRoots)
			mounted |= IsPathWithinRoot(source, fs::canonical(root));
		if (mounted && !review && !approved)
			return true;
		auto scan = request;
		if (!mounted) {
			scan.project.externalSourceRoots.clear();
			scan.project.contentRoot = source.parent_path();
			/* 素材目录与相邻纹理目录可以共享一个上级；避免把整个盘符当成导入范围。 */
			const auto parent = scan.project.contentRoot.parent_path();
			if (parent.has_relative_path() && !IsPathWithinRoot(content, parent) &&
			    !IsPathWithinRoot(request.project.cookedRoot, parent) &&
			    !IsPathWithinRoot(request.project.derivedDataRoot, parent))
				scan.project.contentRoot = parent;
		}
		AssetBuildContext context(std::move(scan));
		static const auto importers = AssetImportRegistry::Defaults();
		const auto* importer = importers.Find(source);
		if (!importer) {
			error = "Unsupported source: " + AssetPathText(source);
			return false;
		}
		if (!context.Initialize(error))
			return false;
		{
			AssetImportResult discovered;
			if (!importer->Import(context, discovered, error))
				return false;
		}
		auto common = source.parent_path();
		/* 已挂载源可跨盘引用其他源根，按原路径构建，无须求复制目录的共同祖先。 */
		if (!mounted) {
			for (const auto& file : context.Dependencies()) {
				if (!IsPathWithinRoot(file.path, context.request.project.contentRoot))
					continue;
				while (!IsPathWithinRoot(file.path, common)) {
					const auto parent = common.parent_path();
					if (parent.empty() || parent == common) {
						error = "Source dependencies do not share a copyable directory: " + AssetPathText(file.path);
						return false;
					}
					common = parent;
				}
			}
		}
		auto target = dest / source.stem();
		/* 直接从资源面板引用单张图片。同名异图各自保存，项目已有素材不被覆盖。 */
		if (!mounted && !review && !approved && TextureImporter::SupportsMaterialMap(source) &&
		    context.Dependencies().size() == 1 && fs::exists(target)) {
			const auto& original = context.Dependencies().front();
			auto alternate = target;
			alternate += "-" + original.sha256.substr(0, 16);
			for (const auto& candidate : {target, alternate}) {
				target = candidate;
				if (!fs::exists(target))
					break;
				bool reused;
				if (!ReuseTexture(request, target, original, reused, error))
					return false;
				if (reused)
					return context.InputsUnchanged(error);
			}
		}
		if (!mounted && fs::exists(target)) {
			error = "Import destination already exists: " + AssetPathText(target);
			return false;
		}
		ProjectImportReview discovered;
		discovered.destination = mounted ? source : target / source.lexically_relative(common);
		for (const auto& dep : context.Dependencies())
			discovered.inputs.emplace(dep.path, dep.sha256);
		for (const auto& diagnostic : context.diagnostics)
			if (diagnostic.level != AssetDiagnosticLevel::Info)
				discovered.warnings.push_back(diagnostic.element + ": " + diagnostic.message);
		if (review) {
			*review = std::move(discovered);
			error.clear();
			return true;
		}
		if (approved && (approved->destination != discovered.destination || approved->inputs != discovered.inputs)) {
			error = "Source or dependencies changed after confirmation; review the import again";
			return false;
		}
		if (mounted)
			return true;
		StableId unique;
		if (!GenerateStableId(unique, error))
			return false;
		auto stage = dest / (".catalpa-import-" + unique.ToString());
		if (!fs::create_directory(stage)) {
			error = "Cannot reserve a temporary import directory";
			return false;
		}
		struct Temp {
			fs::path path;
			bool owned = true;
			~Temp() {
				if (owned) {
					std::error_code ignored;
					fs::remove_all(path, ignored);
				}
			}
		} temp{stage};
		for (const auto& dep : context.Dependencies()) {
			if (!context.CheckCancelled(error))
				return false;
			if (!IsPathWithinRoot(dep.path, common))
				continue;
			const auto relative = dep.path.lexically_relative(common);
			const auto copy = stage / relative;
			fs::create_directories(copy.parent_path());
			fs::copy_file(dep.path, copy, fs::copy_options::none);
			VirtualFileSystem files;
			VirtualPath virtualPath;
			std::vector<std::byte> bytes;
			if (!files.MountReadOnly("import", stage, 0, error) ||
			    !VirtualPath::Parse("import:/" + AssetPathText(relative), virtualPath, error) ||
			    !files.ReadFile(virtualPath, request.settings.maxSourceBytes, bytes, error))
				return false;
			if (AssetBuildDigest(bytes) != dep.sha256) {
				error = "Source changed during import: " + AssetPathText(dep.path);
				return false;
			}
		}
		auto next = target / source.lexically_relative(common);
		if (!context.InputsUnchanged(error) || !context.CheckCancelled(error))
			return false;
		fs::rename(stage, target);
		temp.owned = false;
		request.source = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& failure) {
		error = failure.what();
		return false;
	}
	bool ReviewProjectSource(
	    AssetBuildRequest request, const fs::path& directory, ProjectImportReview& review, std::string& error) {
		return ProcessSource(request, directory, &review, nullptr, error);
	}

	bool ImportProjectSource(AssetBuildRequest& request, const fs::path& directory, std::string& error,
	    const ProjectImportReview* approved) {
		return ProcessSource(request, directory, nullptr, approved, error);
	}

}
