#include "catpch.h"

#include "Commands/PackageCommands.h"

#include "Packager/PackageBuilder.h"
#include "AssetInspector/AssetInspection.h"
#include "Assets/PackageReader.h"

namespace Catalpa::Tools {
	namespace {
		/**
		 * @brief 校验检查命令参数，读取目标文件并收集供命令行输出的资产诊断。
		 *
		 * @param[in] args 调用时传入的参数集合。
		 * @param[in] cancellation 用于查询取消请求的令牌。
		 * @param[in] diagnostics 诊断信息。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Inspect(std::span<const std::string> args, CancellationToken cancellation,
		    std::vector<std::string>& diagnostics, std::string& error) {
			if (cancellation.IsCancellationRequested()) {
				error = "Asset inspection cancelled";
				return false;
			}
			if (args.size() < 2) {
				error = "Expected --inspect <absolute-file-or-Cooked-directory> [--mount name=absolute-root] "
				        "[--entry asset-id]";
				return false;
			}
			auto path = std::filesystem::u8path(args[1]);
			if (!path.is_absolute()) {
				error = "Inspection path must be absolute";
				return false;
			}
			VirtualFileSystem files;
			std::set<std::string> mounts;
			std::vector<AssetId> roots;
			for (size_t index = 2; index < args.size(); ++index) {
				const auto option = args[index];
				if (++index == args.size()) {
					error = "Missing inspection option value";
					return false;
				}
				if (option == "--mount") {
					const auto equal = args[index].find('=');
					const auto name = args[index].substr(0, equal);
					if (equal == std::string::npos || !mounts.insert(name).second ||
					    !files.MountReadOnly(name, std::filesystem::u8path(args[index].substr(equal + 1)), 0, error)) {
						if (error.empty())
							error = "Invalid or duplicate inspection mount";
						return false;
					}
				} else if (option == "--entry" && roots.empty()) {
					AssetId id;
					if (!AssetId::TryParse(args[index], id)) {
						error = "Invalid inspection entry asset ID";
						return false;
					}
					roots.push_back(id);
				} else {
					error = "Unknown or duplicate inspection option: " + option;
					return false;
				}
			}
			AssetRegistry catalog;
			std::shared_ptr<const PackageReader> package;
			if (path.extension() == L".catpkg") {
				if (!mounts.empty()) {
					error = "Package inspection uses only the package's own namespace";
					return false;
				}
				if (!PackageReader::Open(path, package, error) || !files.MountPackage(package, error))
					return false;
				catalog = package->Catalog();
				diagnostics.push_back("Package entry scene: " + package->EntryScene().ToString());
			} else {
				if (std::filesystem::is_directory(path))
					path /= L"Assets.manifest";
				if (path.extension() != L".manifest") {
					if (!mounts.empty() || !roots.empty()) {
						error = "Single-container inspection cannot resolve a closure; supply its manifest";
						return false;
					}
					AssetInspection report;
					if (!AssetInspection::Read(path, report, error))
						return false;
					diagnostics.push_back(report.type + " " + report.id.ToString() + " version=" +
					    std::to_string(report.version) + " sha256=" + report.sha256 + " domain=validated");
					return true;
				}
				VirtualFileSystem metadata;
				VirtualPath virtualPath;
				std::vector<std::byte> bytes;
				const auto name = path.filename().u8string();
				if (!metadata.MountReadOnly("manifest", path.parent_path(), 0, error) ||
				    !VirtualPath::Parse(
				        "manifest:/" + std::string(reinterpret_cast<const char*>(name.data()), name.size()),
				        virtualPath, error) ||
				    !metadata.ReadFile(virtualPath, 64 * 1024 * 1024, bytes, error) ||
				    !AssetManifest::Read(bytes, catalog, error))
					return false;
				/* 标准 Cooked 目录的各 namespace 共享此根；其他根必须由调用者显式挂载。 */
				for (const auto& record : catalog.GetRecords()) {
					const auto& mount = record.locator.path.MountName();
					if (mounts.insert(mount).second && !files.MountReadOnly(mount, path.parent_path(), 0, error))
						return false;
				}
			}
			std::vector<AssetInspection> reports;
			if (!AssetInspection::ReadClosure(catalog, files, roots, reports, error, cancellation))
				return false;
			for (const auto& report : reports)
				diagnostics.push_back(report.type + " " + report.id.ToString() +
				    " version=" + std::to_string(report.version) + " sha256=" + report.sha256 + " domain=validated");
			diagnostics.push_back("Validated " + std::to_string(reports.size()) +
			    " domain payloads and their exact dependency closure; no window/GPU initialized");
			return true;
		}
	} /* 命名空间 */

	ToolCommandResult RunPackageCommand(std::span<const std::string> args, CancellationToken cancellation) {
		ToolCommandResult result;
		std::string error;
		auto fail = [&](std::string message, int code = 1) {
			result.exitCode = cancellation.IsCancellationRequested() ? 130 : code;
			result.diagnostics.push_back(std::move(message));
			return result;
		};
		try {
			if (!args.empty() && args[0] == "--inspect") {
				if (!Inspect(args, cancellation, result.diagnostics, error))
					return fail(error);
				result.exitCode = 0;
				return result;
			}
			if (args.empty() || args[0] != "--package")
				return fail("Expected --inspect <absolute-file> or --package", 2);
			std::map<std::string, std::string> options;
			VirtualFileSystem files;
			std::set<std::string> mounts;
			for (size_t i = 1; i < args.size(); ++i) {
				const auto key = args[i];
				if (++i == args.size())
					return fail("Missing option value: " + key, 2);
				if (key == "--mount") {
					const auto equal = args[i].find('=');
					if (equal == std::string::npos || !mounts.insert(args[i].substr(0, equal)).second ||
					    !files.MountReadOnly(
					        args[i].substr(0, equal), std::filesystem::u8path(args[i].substr(equal + 1)), 0, error))
						return fail("Invalid or duplicate --mount: " + error, 2);
				} else if ((key != "--manifest" && key != "--entry" && key != "--output") ||
				    !options.emplace(key, args[i]).second)
					return fail("Unknown or duplicate package option: " + key, 2);
			}
			if (options.size() != 3)
				return fail("Require --manifest, --entry, --output and --mount name=absolute-root", 2);
			const auto manifestPath = std::filesystem::u8path(options.at("--manifest"));
			AssetId entry;
			if (!manifestPath.is_absolute() || !AssetId::TryParse(options.at("--entry"), entry))
				return fail("Invalid manifest path or scene ID", 2);
			VirtualFileSystem metadata;
			VirtualPath virtualPath;
			std::vector<std::byte> manifest;
			const auto filename = manifestPath.filename().u8string();
			if (!metadata.MountReadOnly("manifest", manifestPath.parent_path(), 0, error) ||
			    !VirtualPath::Parse(
			        "manifest:/" + std::string(reinterpret_cast<const char*>(filename.data()), filename.size()),
			        virtualPath, error) ||
			    !metadata.ReadFile(virtualPath, 16 * 1024 * 1024, manifest, error))
				return fail(error);
			AssetRegistry catalog;
			if (!AssetManifest::Read(manifest, catalog, error))
				return fail(error);
			/*
			 * 与 inspect 一致：未显式覆盖的 Cooked namespace 仅解析到清单所在根。
			 * 不依赖源绝对路径；异地资源必须用 --mount 明确提供。
			 */
			for (const auto& record : catalog.GetRecords()) {
				const auto& mount = record.locator.path.MountName();
				if (mounts.insert(mount).second && !files.MountReadOnly(mount, manifestPath.parent_path(), 0, error))
					return fail(error);
			}
			const auto output = std::filesystem::u8path(options.at("--output"));
			if (!PackageBuilder::Build(catalog, files, entry, output, cancellation, error))
				return fail(error);
			result.outputs.push_back(output);
			result.exitCode = 0;
			result.diagnostics.push_back("Package published with validated dependency closure");
			return result;
		} catch (const std::exception& e) {
			return fail(e.what());
		}
	}
} /* 命名空间 Catalpa::Tools */
