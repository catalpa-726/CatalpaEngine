#include "catpch.h"

#include "Navigation/NavMeshAssetLoader.h"

#include "Navigation/NavMeshAsset.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa {
	namespace {
		using AuthorJson::Json;

		Json Settings(const NavigationBuildSettings& s) {
			return {{"cellSize", s.cellSize}, {"cellHeight", s.cellHeight}, {"agentHeight", s.agentHeight},
			    {"agentRadius", s.agentRadius}, {"maxClimb", s.maxClimb}, {"maxSlopeDegrees", s.maxSlopeDegrees},
			    {"maxCells", s.maxCells}};
		}

		NavigationBuildSettings Settings(const Json& j) {
			AuthorJson::Fields(
			    j, {"cellSize", "cellHeight", "agentHeight", "agentRadius", "maxClimb", "maxSlopeDegrees", "maxCells"});
			NavigationBuildSettings s;
			s.cellSize = float(AuthorJson::Number(j.at("cellSize")));
			s.cellHeight = float(AuthorJson::Number(j.at("cellHeight")));
			s.agentHeight = float(AuthorJson::Number(j.at("agentHeight")));
			s.agentRadius = float(AuthorJson::Number(j.at("agentRadius")));
			s.maxClimb = float(AuthorJson::Number(j.at("maxClimb")));
			s.maxSlopeDegrees = float(AuthorJson::Number(j.at("maxSlopeDegrees")));
			const auto max = AuthorJson::Unsigned(j.at("maxCells"));
			if (max > 1000000)
				throw std::invalid_argument("Navigation cell budget exceeds one million");
			s.maxCells = uint32_t(max);
			return s;
		}

		bool ValidSettings(const NavigationBuildSettings& s) {
			return std::isfinite(s.cellSize) && s.cellSize >= 0.01f && s.cellSize <= 10 &&
			    std::isfinite(s.cellHeight) && s.cellHeight >= 0.01f && s.cellHeight <= 10 &&
			    std::isfinite(s.agentHeight) && s.agentHeight >= s.cellHeight * 3 && s.agentHeight <= 100 &&
			    std::isfinite(s.agentRadius) && s.agentRadius >= 0 && s.agentRadius <= 20 &&
			    std::isfinite(s.maxClimb) && s.maxClimb >= 0 && s.maxClimb <= 10 && std::isfinite(s.maxSlopeDegrees) &&
			    s.maxSlopeDegrees >= 0 && s.maxSlopeDegrees < 90 && s.maxCells > 0 && s.maxCells <= 1000000;
		}
	} /* 命名空间 */
	bool ValidateNavigationSettings(const NavigationBuildSettings& settings, std::string& error) {
		if (!ValidSettings(settings)) {
			error = "Navigation build settings exceed the bounded profile";
			return false;
		}
		error.clear();
		return true;
	}

	bool ValidateNavigationInput(const NavigationBuildInput& input, std::string& error) {
		if (!ValidateNavigationSettings(input.settings, error))
			return false;
		if (input.vertices.size() < 3 || input.vertices.size() > 65536 || input.indices.empty() ||
		    input.indices.size() % 3 || input.indices.size() > 196608 ||
		    (!input.areas.empty() && input.areas.size() != input.indices.size() / 3)) {
			error = "Navigation settings, geometry or area count exceeds the bounded build profile";
			return false;
		}
		for (const auto v : input.vertices)
			if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
			    std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) > 1e5f) {
				error = "Navigation source requires finite metre-space positions within 100km";
				return false;
			}
		for (size_t n = 0; n < input.indices.size(); n += 3) {
			const auto a = input.indices[n], b = input.indices[n + 1], c = input.indices[n + 2];
			if (a >= input.vertices.size() || b >= input.vertices.size() || c >= input.vertices.size()) {
				error = "Navigation triangle index is out of range";
				return false;
			}
			const auto normal = glm::cross(glm::dvec3(input.vertices[b]) - glm::dvec3(input.vertices[a]),
			    glm::dvec3(input.vertices[c]) - glm::dvec3(input.vertices[a]));
			if (glm::dot(normal, normal) == 0) {
				error = "Navigation source contains a degenerate triangle";
				return false;
			}
		}
		for (auto area : input.areas)
			if (area > 62) {
				error = "Navigation area must be 0..62";
				return false;
			}
		error.clear();
		return true;
	}

	bool ReadNavigationSource(std::string_view text, NavigationBuildInput& output, std::string& error) {
		return ReadNavigationSource(text, NavigationBuildSettings{}, output, error);
	}

	bool ReadNavigationSource(std::string_view text, const NavigationBuildSettings& defaults,
	    NavigationBuildInput& output, std::string& error) try {
		std::unique_ptr<ArchiveValue> checked;
		ArchiveError archiveError;
		ArchiveLimits limits;
		limits.maxBytes = 16 * 1024 * 1024;
		limits.maxNodes = 1000000;
		limits.maxCollectionEntries = 262144;
		if (!JsonArchive::Parse(text, checked, archiveError, limits))
			throw std::invalid_argument(archiveError.ToString());
		const auto j = Json::parse(text);
		AuthorJson::Fields(j, {"schemaVersion", "profile", "settings", "vertices", "indices", "areas"});
		if (AuthorJson::Unsigned(j.at("schemaVersion")) != 1)
			throw std::invalid_argument("Unsupported navigation source schema");
		NavigationBuildInput next;
		auto settings = Settings(defaults);
		if (j.contains("settings")) {
			if (!j["settings"].is_object())
				throw std::invalid_argument("Navigation settings must be an object");
			for (const auto& [key, value] : j["settings"].items())
				settings[key] = value;
		}
		next.settings = Settings(settings);
		if (j.contains("profile")) {
			next.buildProfile = j.at("profile").get<std::string>();
			if (next.buildProfile.empty() || next.buildProfile.size() > 64)
				throw std::invalid_argument("Navigation profile name is invalid");
		}
		if (!j.at("vertices").is_array() || j.at("vertices").size() > 65536 || !j.at("indices").is_array() ||
		    j.at("indices").size() > 196608)
			throw std::invalid_argument("Navigation geometry budget");
		for (const auto& v : j.at("vertices"))
			next.vertices.push_back(AuthorJson::Vec3(v));
		for (const auto& v : j.at("indices")) {
			const auto n = AuthorJson::Unsigned(v);
			if (n > UINT32_MAX)
				throw std::invalid_argument("Navigation index overflow");
			next.indices.push_back(uint32_t(n));
		}
		if (j.contains("areas"))
			for (const auto& v : j.at("areas")) {
				const auto n = AuthorJson::Unsigned(v);
				if (n > 62)
					throw std::invalid_argument("Navigation area overflow");
				next.areas.push_back(uint8_t(n));
			}
		if (!ValidateNavigationInput(next, error))
			return false;
		output = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool WriteNavigationSource(const NavigationBuildInput& value, std::string& output, std::string& error) try {
		if (!ValidateNavigationInput(value, error))
			return false;
		Json j{{"schemaVersion", 1}, {"settings", Settings(value.settings)}, {"vertices", Json::array()},
		    {"indices", value.indices}, {"areas", value.areas}};
		if (!value.buildProfile.empty())
			j["profile"] = value.buildProfile;
		for (const auto v : value.vertices)
			j["vertices"].push_back(AuthorJson::Array(v));
		output = j.dump();
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool ReadNavMeshAsset(std::string_view text, NavMeshAsset& output, std::string& error) try {
		const auto j = AuthorJson::Parse(text, 32 * 1024 * 1024);
		AuthorJson::Fields(j, {"schemaVersion", "abi", "settings", "tileHex"});
		if (AuthorJson::Unsigned(j.at("schemaVersion")) != 1 || j.at("abi") != "detour-v7-le-polyref32")
			throw std::invalid_argument("Unsupported cooked navigation ABI");
		NavMeshAsset next;
		next.settings = Settings(j.at("settings"));
		if (!ValidSettings(next.settings))
			throw std::invalid_argument("Invalid navigation build profile");
		const auto hex = j.at("tileHex").get<std::string>();
		if (hex.empty() || hex.size() % 2 || hex.size() > 16 * 1024 * 1024)
			throw std::invalid_argument("Cooked navigation tile size rejected");
		auto digit = [](char c) -> int {
			return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		};
		next.tile.reserve(hex.size() / 2);
		for (size_t i = 0; i < hex.size(); i += 2) {
			const auto a = digit(hex[i]), b = digit(hex[i + 1]);
			if (a < 0 || b < 0)
				throw std::invalid_argument("Malformed navigation hex payload");
			next.tile.push_back(std::byte((a << 4) | b));
		}
		if (!ValidateNavMeshAsset(next, error))
			return false;
		output = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool WriteNavMeshAsset(const NavMeshAsset& value, std::string& output, std::string& error) try {
		if (!ValidSettings(value.settings) || !ValidateNavMeshAsset(value, error))
			return false;
		std::string hex;
		hex.reserve(value.tile.size() * 2);
		constexpr char digits[] = "0123456789abcdef";
		for (auto b : value.tile) {
			const auto n = std::to_integer<uint8_t>(b);
			hex += digits[n >> 4];
			hex += digits[n & 15];
		}
		output = Json{{"schemaVersion", 1}, {"abi", "detour-v7-le-polyref32"}, {"settings", Settings(value.settings)},
		    {"tileHex", std::move(hex)}}
		             .dump();
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool RegisterNavigationAssetLoader(AssetManager& manager, std::string& error) {
		AssetLoader loader;
		if (!AssetType::TryParse("navmesh", loader.type)) {
			error = "Invalid navmesh type key";
			return false;
		}
		loader.cppType = typeid(NavMeshAsset);
		loader.decode = [](const AssetContainer& c, const AssetDecodeContext& ctx, AssetLoadResult& output,
		                    std::string& error) {
			if (c.payloadVersion != 1 || !c.dependencies.empty() || ctx.cancellation.IsCancellationRequested() ||
			    c.payload.size() > ctx.maxResidentBytes / 5) {
				error = "Navigation decode version, cancellation or resident budget failure";
				return false;
			}
			auto value = std::make_shared<NavMeshAsset>();
			if (!ReadNavMeshAsset({reinterpret_cast<const char*>(c.payload.data()), c.payload.size()}, *value, error))
				return false;
			output = MakeAssetLoadResult<NavMeshAsset>(value, sizeof(NavMeshAsset) + value->tile.capacity());
			return true;
		};
		return manager.RegisterLoader(std::move(loader), error);
	}
} /* 命名空间 Catalpa */
