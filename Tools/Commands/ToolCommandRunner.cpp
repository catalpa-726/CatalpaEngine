#include "catpch.h"

#include "Commands/PackageCommands.h"
#include "Commands/ToolCommandRunner.h"

#include "AssetPipeline/Importers/ShaderImporter.h"
#include "AssetPipeline/Cookers/ShaderCooker.h"
#include "Shaders/ShaderLibrary.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetManifest.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "AssetCompiler/AssetCompilerOptions.h"
#include "AssetPipeline/AssetCooker.h"
#include "MaterialGraph/MaterialGraphCompiler.h"
namespace Catalpa::Tools {
	ToolCommandResult ToolCommandRunner::Run(std::span<const std::string> args, CancellationToken cancellation) {
		if (!args.empty() && (args[0] == "--inspect" || args[0] == "--package"))
			return RunPackageCommand(args, cancellation);
		ToolCommandResult result;
		auto fail = [&](std::string message, int code = 1) {
			result.exitCode = cancellation.IsCancellationRequested() ? 130 : code;
			result.diagnostics.push_back(std::move(message));
			return result;
		};
		try {
			if (!args.empty() && (args[0] == "--build-assets" || args[0] == "--cook-assets")) {
				AssetBuildRequest request;
				std::string error;
				if (!AssetCompilerOptions::Parse(args, request, error))
					return fail(error, 2);
				request.cancellation = cancellation;
				const auto built = AssetCooker::Build(std::move(request));
				for (const auto& d : built.diagnostics)
					result.diagnostics.push_back(d.element + ": " + d.message);
				if (!built.success)
					return fail(built.error);
				result.exitCode = 0;
				result.outputs = {built.manifest, built.report};
				result.diagnostics.push_back(std::string(built.cacheHit ? "Cache hit: " : "Cooked: ") +
				    std::to_string(built.records.size()) + " assets; primary=" + built.primary.ToString());
				result.diagnostics.push_back("Catalog: " + AssetPathText(built.manifest));
				result.diagnostics.push_back("Source mapping and conversion report: " + AssetPathText(built.report));
				return result;
			}
			if (args.empty() || args[0] != "--cook-shaders")
				return fail("expected --cook-assets/--build-assets --project <file> --source <file> "
				            "--shader-root <directory>, or --cook-shaders --source-root <directory> "
				            "--output-root <directory> [--sdk-root <directory>] [--define NAME=VALUE]",
				    2);
			std::filesystem::path source, output, sdk;
			std::map<std::string, std::string, std::less<>> defines;
			std::set<std::string> seen;
			for (size_t i = 1; i < args.size(); ++i) {
				const auto& option = args[i];
				if (i + 1 == args.size())
					return fail("missing value for " + option, 2);
				const auto& value = args[++i];
				if (option == "--define") {
					const auto equal = value.find('=');
					if (equal == std::string::npos ||
					    !defines.emplace(value.substr(0, equal), value.substr(equal + 1)).second)
						return fail("--define must be unique NAME=VALUE", 2);
					continue;
				}
				if (!seen.insert(option).second)
					return fail("duplicate argument: " + option, 2);
				if (option == "--source-root")
					source = std::filesystem::u8path(value);
				else if (option == "--output-root")
					output = std::filesystem::u8path(value);
				else if (option == "--sdk-root")
					sdk = std::filesystem::u8path(value);
				else
					return fail("unknown shader option: " + option, 2);
			}
			if (sdk.empty()) {
				const auto* environment = _wgetenv(L"VULKAN_SDK");
				if (environment)
					sdk = environment;
			}
			if (!source.is_absolute() || !output.is_absolute() || !sdk.is_absolute())
				return fail("source/output roots and explicit SDK or VULKAN_SDK must be absolute", 2);
			source = std::filesystem::canonical(source);
			sdk = std::filesystem::canonical(sdk);
			output = std::filesystem::weakly_canonical(output);
			if (IsPathWithinRoot(output, source) || IsPathWithinRoot(source, output) || IsPathWithinRoot(output, sdk) ||
			    IsPathWithinRoot(sdk, output))
				return fail("shader derived output must not overlap source or SDK roots", 2);
			std::vector<std::filesystem::path> sources;
			for (const auto& file : std::filesystem::recursive_directory_iterator(source)) {
				if (file.is_symlink())
					return fail("shader tree contains a symbolic link");
				if (!file.is_regular_file() || file.path().extension() != L".slang")
					continue;
				const auto relative = file.path().lexically_relative(source);
				const auto text = ShaderToolDetail::PathText(relative);
				std::string first = ShaderToolDetail::PathText(*relative.begin());
				std::transform(first.begin(), first.end(), first.begin(),
				    [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
				if (first == "common")
					continue; /* Common 用作模块分组；其余入口名和阶段由 Slang 识别。 */
				sources.push_back(relative);
			}
			if (sources.empty() || sources.size() > 1024)
				return fail("shader entry module count must be 1..1024");
			std::sort(sources.begin(), sources.end());
			AssetRegistry previous;
			std::vector<std::byte> previousBytes;
			std::string previousError;
			const auto previousManifest = output / L"BuiltinShaders.manifest";
			if (std::filesystem::is_regular_file(previousManifest) &&
			    ShaderToolDetail::ReadFile(previousManifest, 4 * 1024 * 1024, previousBytes, previousError))
				if (!AssetManifest::Read(previousBytes, previous, previousError))
					result.diagnostics.push_back("Ignoring invalid old shader manifest: " + previousError);
			struct Cooked {
				std::filesystem::path relative;
				std::vector<std::byte> bytes;
				bool unchanged = false;
			};
			std::vector<Cooked> cooked;
			std::vector<AssetRecord> records;
			std::vector<std::string> fingerprints;
			size_t total = 0;
			for (const auto& relative : sources) {
				if (cancellation.IsCancellationRequested())
					return fail("shader cook cancelled");
				ShaderCompileRequest request{source, source / relative, sdk, defines, cancellation};
				ShaderCompileResult imported;
				std::string error;
				AssetId id;
				if (!ShaderLibrary::BuiltinId(ShaderToolDetail::PathText(relative), id, error))
					return fail(error);
				Cooked item;
				item.relative = relative;
				item.relative.replace_extension(L".catasset");
				std::string fingerprint;
				if (!ShaderCompileJob::Fingerprint(request, fingerprint, error))
					return fail(error);
				fingerprints.push_back(fingerprint);
				const auto* oldRecord = previous.Find(id);
				if (oldRecord && oldRecord->type.ToString() == "shader") {
					const auto existing = output / std::filesystem::u8path(oldRecord->locator.path.RelativePath());
					std::string cacheError, containerHash;
					AssetContainer cached;
					std::shared_ptr<const ShaderAsset> cachedAsset;
					if (IsPathWithinRoot(std::filesystem::weakly_canonical(existing), output) &&
					    ShaderToolDetail::ReadFile(existing, 64 * 1024 * 1024, item.bytes, cacheError) &&
					    ComputeAssetContentHash(item.bytes, containerHash, cacheError) &&
					    containerHash == oldRecord->contentHash &&
					    AssetContainerReader::Read(item.bytes, cached, cacheError) && cached.id == id &&
					    cached.type.ToString() == "shader" && cached.version == oldRecord->version &&
					    cached.payloadVersion == 1 && cached.dependencies.empty() &&
					    ShaderLibrary::DecodePayload(cached.payload, cachedAsset, cacheError) &&
					    cachedAsset->variant.inputFingerprint == fingerprint) {
						item.relative = std::filesystem::path(L"versions") /
						    std::filesystem::u8path(cachedAsset->variant.sha256) / item.relative;
						item.unchanged =
						    ShaderToolDetail::PathText(item.relative) == oldRecord->locator.path.RelativePath();
						if (item.unchanged)
							result.diagnostics.push_back("Shader unchanged: " + ShaderToolDetail::PathText(relative));
						else {
							item.relative = relative;
							item.relative.replace_extension(L".catasset");
						}
					}
				}
				if (!item.unchanged) {
					if (!ShaderImporter::Import(request, imported, error))
						return fail(ShaderToolDetail::PathText(relative) + ": " + error);
					if (imported.asset->variant.inputFingerprint != fingerprint)
						return fail("shader inputs changed during cache/compile decision");
					if (!ShaderCooker::Cook(id, *imported.asset, item.bytes, error))
						return fail(error);
					item.relative = std::filesystem::path(L"versions") /
					    std::filesystem::u8path(imported.asset->variant.sha256) / item.relative;
				}
				if (item.bytes.size() > 256 * 1024 * 1024 - total)
					return fail("shader batch exceeds 256MiB");
				total += item.bytes.size();
				AssetContainer header;
				if (!AssetContainerReader::Read(item.bytes, header, error))
					return fail(error);
				AssetRecord record;
				record.id = header.id;
				record.type = header.type;
				record.version = header.version;
				record.locator.kind = AssetLocationKind::Cooked;
				if (!VirtualPath::Parse(
				        "builtin-shaders:/" + ShaderToolDetail::PathText(item.relative), record.locator.path, error) ||
				    !ComputeAssetContentHash(item.bytes, record.contentHash, error))
					return fail(error);
				records.push_back(std::move(record));
				cooked.push_back(std::move(item));
				if (!imported.diagnostics.empty())
					result.diagnostics.push_back(std::move(imported.diagnostics));
			}
			for (size_t i = 0; i < sources.size(); ++i) {
				std::string current, error;
				if (!ShaderCompileJob::Fingerprint(
				        {source, source / sources[i], sdk, defines, cancellation}, current, error))
					return fail(error);
				if (current != fingerprints[i])
					return fail("shader batch source/tool inputs changed before publication");
			}
			AssetRegistry registry;
			std::vector<std::byte> manifest;
			std::string error;
			if (!registry.Replace(records, error) || !AssetManifest::Write(registry, manifest, error))
				return fail(error);
			if (cancellation.IsCancellationRequested())
				return fail("shader cook cancelled before output publication");
			/* 先写完各个不可变的版本化产物，再原子替换清单。在最后一次替换之前，旧清单仍可使用。 */
			std::filesystem::create_directories(output);
			for (const auto& item : cooked) {
				if (cancellation.IsCancellationRequested())
					return fail("shader cook cancelled before manifest publication");
				const auto target = output / item.relative;
				if (item.unchanged) {
					result.outputs.push_back(target);
					continue;
				}
				std::filesystem::create_directories(target.parent_path());
				if (!IsPathWithinRoot(std::filesystem::canonical(target.parent_path()), output))
					return fail("output parent escaped derived root");
				if (!AtomicFileWriter::Write(target, item.bytes, error))
					return fail(error);
				result.outputs.push_back(target);
			}
			if (cancellation.IsCancellationRequested())
				return fail("shader cook cancelled before manifest publication");
			if (!MaterialGraphCompiler::DeployTemplates(source, output.parent_path() / L"ShaderSources", error))
				return fail(error);
			const auto manifestPath = output / L"BuiltinShaders.manifest";
			std::vector<std::byte> oldManifest;
			std::string ignored;
			const bool manifestUnchanged = std::filesystem::is_regular_file(manifestPath) &&
			    ShaderToolDetail::ReadFile(manifestPath, 4 * 1024 * 1024, oldManifest, ignored) &&
			    oldManifest == manifest;
			if (!manifestUnchanged && !AtomicFileWriter::Write(manifestPath, manifest, error))
				return fail(error);
			result.outputs.push_back(manifestPath);
			result.exitCode = 0;
			result.diagnostics.push_back("Cooked " + std::to_string(cooked.size()) +
			    " shader modules; final SPIR-V validated, reflected and hashed.");
			return result;
		} catch (const std::exception& e) {
			return fail(e.what());
		}
	}
}
