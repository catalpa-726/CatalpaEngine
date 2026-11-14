#include "AssetPipeline/Navigation/NavigationBuildProfiles.h"

#include "Core/Serialization/AuthorJson.h"

namespace Catalpa::Tools {
	bool ResolveNavigationBuildSource(
	    std::string_view source, std::string_view profiles, NavigationBuildInput& output, std::string& error) try {
		const auto config = AuthorJson::Parse(profiles, 65536);
		AuthorJson::Fields(config, {"schemaVersion", "default", "profiles"});
		if (AuthorJson::Unsigned(config.at("schemaVersion")) != 1 || !config.at("profiles").is_object() ||
		    config["profiles"].empty() || config["profiles"].size() > 32)
			throw std::invalid_argument("Navigation build profiles schema/count rejected");
		std::map<std::string, NavigationBuildSettings, std::less<>> candidates;
		for (const auto& [name, settings] : config["profiles"].items()) {
			if (name.empty() || name.size() > 64)
				throw std::invalid_argument("Invalid navigation build profile identity");
			if (!settings.is_object() || settings.size() != 7)
				throw std::invalid_argument("Each navigation profile requires all seven settings");
			for (const auto* field :
			    {"cellSize", "cellHeight", "agentHeight", "agentRadius", "maxClimb", "maxSlopeDegrees", "maxCells"})
				if (!settings.contains(field))
					throw std::invalid_argument("Incomplete navigation profile");
			/* 复用领域设置验证器，最小非退化三角形只用于检查profile，绝不用于实际烘焙。 */
			const auto check = AuthorJson::Json{{"schemaVersion", 1}, {"settings", settings},
			    {"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}}, {"indices", {0, 2, 1}}};
			NavigationBuildInput value;
			if (!ReadNavigationSource(check.dump(), value, error))
				return false;
			candidates.emplace(name, value.settings);
		}
		auto selected = config.at("default").get<std::string>();
		if (!candidates.contains(selected))
			throw std::invalid_argument("Default navigation build profile is missing");
		/* 几何可以超过通用JSON集合默认值，严格解析交由领域源读取器；这里只读有界profile字段。 */
		if (source.size() > 16 * 1024 * 1024)
			throw std::invalid_argument("Navigation source exceeds 16 MiB");
		{
			std::unique_ptr<ArchiveValue> checked;
			ArchiveError issue;
			ArchiveLimits limits;
			limits.maxBytes = 16 * 1024 * 1024;
			limits.maxNodes = 1000000;
			limits.maxCollectionEntries = 262144;
			if (!JsonArchive::Parse(source, checked, issue, limits))
				throw std::invalid_argument(issue.ToString());
		}
		const auto author = AuthorJson::Json::parse(source);
		if (author.contains("profile"))
			selected = author.at("profile").get<std::string>();
		const auto chosen = candidates.find(selected);
		if (chosen == candidates.end())
			throw std::invalid_argument("Unknown navigation build profile: " + selected);
		NavigationBuildInput next;
		if (!ReadNavigationSource(source, chosen->second, next, error))
			return false;
		if (next.buildProfile.empty())
			next.buildProfile = selected;
		output = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Tools */
