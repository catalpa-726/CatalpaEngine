#include "catpch.h"

#include "AssetCompiler/AssetCompilerOptions.h"

namespace Catalpa::Tools {
	bool AssetCompilerOptions::Parse(std::span<const std::string> args, AssetBuildRequest& output, std::string& error) {
		try {
			if (args.empty() || (args.front() != "--build-assets" && args.front() != "--cook-assets")) {
				error = "Expected --build-assets or --cook-assets";
				return false;
			}
			AssetBuildRequest request;
			std::filesystem::path project;
			std::set<std::string> seen;
			for (size_t i = 1; i < args.size(); ++i) {
				const auto& option = args[i];
				if (option == "--remap") {
					if (++i == args.size()) {
						error = "Missing URI=path after --remap";
						return false;
					}
					const auto equal = args[i].find('=');
					if (equal == std::string::npos || equal == 0 || equal + 1 == args[i].size() ||
					    !request.settings.dependencyRemaps.emplace(args[i].substr(0, equal), args[i].substr(equal + 1))
					        .second) {
						error = "--remap requires a unique nonempty URI=path";
						return false;
					}
					continue;
				}
				if (!seen.insert(option).second) {
					error = "Duplicate asset option: " + option;
					return false;
				}
				if (option == "--no-generate-normals") {
					request.settings.generateNormals = false;
					continue;
				}
				if (option == "--no-generate-tangents") {
					request.settings.generateTangents = false;
					continue;
				}
				if (++i == args.size()) {
					error = "Missing value for " + option;
					return false;
				}
				const auto& value = args[i];
				if (option == "--project")
					project = std::filesystem::u8path(value);
				else if (option == "--source")
					request.source = std::filesystem::u8path(value);
				else if (option == "--shader-root")
					request.shaderRoot = std::filesystem::u8path(value);
				else if (option == "--source-up") {
					if (value != "auto" && value != "y" && value != "z") {
						error = "--source-up accepts auto/y/z";
						return false;
					}
					request.settings.sourceUp = value;
				} else if (option == "--unit-scale") {
					size_t used = 0;
					const auto scale = std::stod(value, &used);
					if (used != value.size() || !std::isfinite(scale) || scale <= 0 || scale > 1e9) {
						error = "--unit-scale requires a finite positive scale <= 1e9";
						return false;
					}
					request.settings.unitScale = scale;
				} else {
					error = "Unknown asset option: " + option;
					return false;
				}
			}
			if (!project.is_absolute() || !request.source.is_absolute() || !request.shaderRoot.is_absolute()) {
				error = "Required: --project <absolute-file> --source <absolute-file> --shader-root "
				        "<absolute-cooked-shaders>";
				return false;
			}
			std::unique_ptr<ProjectSession> session;
			if (!LoadProject(project, session, error))
				return false;
			request.project = session->config;
			output = std::move(request);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}
}
