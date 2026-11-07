#include "catpch.h"
#include "Terrain/TerrainAsset.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa {
    namespace {
        using AuthorJson::Json;
        Json Settings(const TerrainSettings& s) {
            return {{"width", s.width},
                    {"height", s.height},
                    {"tileQuads", s.tileQuads},
                    {"lodCount", s.lodCount},
                    {"spacing", s.spacing},
                    {"heightScale", s.heightScale},
                    {"heightOffset", s.heightOffset},
                    {"skirtDepth", s.skirtDepth},
                    {"buildCollision", s.buildCollision},
                    {"buildNavigation", s.buildNavigation}};
        }
        TerrainSettings Settings(const Json& j) {
            AuthorJson::Fields(j, {"width", "height", "tileQuads", "lodCount", "spacing", "heightScale",
                                   "heightOffset", "skirtDepth", "buildCollision", "buildNavigation"});
            TerrainSettings s;
            const auto integer = [&](const char* key) {
                auto n = AuthorJson::Unsigned(j.at(key));
                if (n > UINT32_MAX)
                    throw std::invalid_argument("Terrain integer overflow");
                return uint32_t(n);
            };
            s.width = integer("width");
            s.height = integer("height");
            s.tileQuads = integer("tileQuads");
            s.lodCount = integer("lodCount");
            s.spacing = float(AuthorJson::Number(j.at("spacing")));
            s.heightScale = float(AuthorJson::Number(j.at("heightScale")));
            s.heightOffset = float(AuthorJson::Number(j.at("heightOffset")));
            s.skirtDepth = float(AuthorJson::Number(j.at("skirtDepth")));
            s.buildCollision = j.at("buildCollision").get<bool>();
            s.buildNavigation = j.at("buildNavigation").get<bool>();
            std::string error;
            if (!ValidateTerrainSettings(s, error))
                throw std::invalid_argument(error);
            return s;
        }
        Json Ref(const AssetDependency& v) {
            return {{"id", v.id.HasValue() ? v.id.ToString() : ""}, {"version", v.version}};
        }
        AssetDependency Ref(const Json& j, bool optional) {
            AuthorJson::Fields(j, {"id", "version"});
            AssetDependency v;
            if (j.at("id") == "") {
                if (!optional)
                    throw std::invalid_argument("Required terrain asset ID is empty");
            } else
                v.id = AuthorJson::Identity<AssetId>(j.at("id"));
            v.version = AuthorJson::Unsigned(j.at("version"));
            return v;
        }
        bool ValidMetadata(const TerrainAsset& v, std::string& error) {
            if (!ValidateTerrainSettings(v.settings, error))
                return false;
            const auto& s = v.settings;
            const size_t expected =
                size_t((s.width - 1) / s.tileQuads) * ((s.height - 1) / s.tileQuads) * s.lodCount;
            if (v.heightDigest.size() != 64 || v.tiles.size() != expected || !v.material.id.HasValue() ||
                (s.buildNavigation && !v.navigation.id.HasValue())) {
                error = "Terrain metadata lacks digest/material/navigation or complete tile grid";
                return false;
            }
            for (char c : v.heightDigest)
                if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) {
                    error = "Terrain height digest is not canonical SHA-256";
                    return false;
                }
            std::set<std::tuple<uint32_t, uint32_t, uint32_t>> keys;
            for (const auto& t : v.tiles)
                if (t.x >= (s.width - 1) / s.tileQuads || t.z >= (s.height - 1) / s.tileQuads ||
                    t.lod >= s.lodCount || !t.mesh.id.HasValue() ||
                    (s.buildCollision && !t.collision.id.HasValue()) || t.bounds.empty ||
                    !ValidateBounds(t.bounds).IsSuccess() || !keys.emplace(t.x, t.z, t.lod).second) {
                    error = "Terrain tile key, dependency or bounds invalid";
                    return false;
                }
            error.clear();
            return true;
        }
    } // namespace
    bool ValidateTerrainSettings(const TerrainSettings& s, std::string& error) {
        if (s.width < 3 || s.width > 2049 || s.height < 3 || s.height > 2049 || s.tileQuads < 2 ||
            s.tileQuads > 128 || (s.tileQuads & (s.tileQuads - 1)) || (s.width - 1) % s.tileQuads ||
            (s.height - 1) % s.tileQuads || !s.lodCount || s.lodCount > 8 ||
            (uint32_t(1) << (s.lodCount - 1)) > s.tileQuads ||
            size_t((s.width - 1) / s.tileQuads) * ((s.height - 1) / s.tileQuads) * s.lodCount > 4096 ||
            !std::isfinite(s.spacing) || s.spacing < 0.01f || s.spacing > 100 ||
            !std::isfinite(s.heightScale) || std::abs(s.heightScale) > 10000 ||
            !std::isfinite(s.heightOffset) || std::abs(s.heightOffset) > 100000 ||
            !std::isfinite(s.skirtDepth) || s.skirtDepth <= 0 || s.skirtDepth > 100000 ||
            (s.buildNavigation && size_t(s.width) * s.height > 65536)) {
            error = "Terrain dimensions/LOD/metre settings exceed the supported bounded profile";
            return false;
        }
        error.clear();
        return true;
    }
    bool ValidateTerrainHeights(const TerrainHeightData& value, std::string& error) {
        if (!ValidateTerrainSettings(value.settings, error))
            return false;
        if (value.heights.size() != size_t(value.settings.width) * value.settings.height) {
            error = "Terrain height sample count mismatch";
            return false;
        }
        float low = std::numeric_limits<float>::max(), high = -low;
        for (float h : value.heights) {
            if (!std::isfinite(h) || std::abs(h) > 100000) {
                error = "Terrain height is nonfinite or outside metre bounds";
                return false;
            }
            low = std::min(low, h);
            high = std::max(high, h);
        }
        if (value.settings.skirtDepth < high - low) {
            error = "Terrain skirtDepth must cover the height range to hide all cross-LOD cracks; source "
                    "heights were "
                    "not altered";
            return false;
        }
        error.clear();
        return true;
    }
    bool ReadTerrainSource(std::string_view text, TerrainSource& output, std::string& error) try {
        const auto j = AuthorJson::Parse(text);
        AuthorJson::Fields(j, {"schemaVersion", "heightSource", "encoding", "settings", "material"});
        if (AuthorJson::Unsigned(j.at("schemaVersion")) != 1)
            throw std::invalid_argument("Unsupported terrain source schema");
        TerrainSource v;
        v.heightSource = j.at("heightSource").get<std::string>();
        v.encoding = j.at("encoding").get<std::string>();
        v.settings = Settings(j.at("settings"));
        if (v.heightSource.empty() || v.heightSource.size() > 4096 ||
            v.heightSource.find('\0') != std::string::npos ||
            (v.encoding != "r16_unorm_le" && v.encoding != "f32_le"))
            throw std::invalid_argument(
                "Terrain requires a bounded R16_UNORM_LE or F32_LE read-only height source");
        if (j.at("material") != "")
            v.material = AuthorJson::Identity<AssetId>(j.at("material"));
        output = std::move(v);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteTerrainSource(const TerrainSource& v, std::string& output, std::string& error) try {
        if (!ValidateTerrainSettings(v.settings, error))
            return false;
        const auto text = Json{{"schemaVersion", 1},
                               {"heightSource", v.heightSource},
                               {"encoding", v.encoding},
                               {"settings", Settings(v.settings)},
                               {"material", v.material.HasValue() ? v.material.ToString() : ""}}
                              .dump();
        TerrainSource checked;
        if (!ReadTerrainSource(text, checked, error))
            return false;
        output = text;
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool ReadTerrainAsset(std::string_view text, TerrainAsset& output, std::string& error) try {
        const auto j = AuthorJson::Parse(text);
        AuthorJson::Fields(j,
                           {"schemaVersion", "settings", "heightDigest", "material", "navigation", "tiles"});
        if (AuthorJson::Unsigned(j.at("schemaVersion")) != 1)
            throw std::invalid_argument("Unsupported cooked terrain schema");
        TerrainAsset v;
        v.settings = Settings(j.at("settings"));
        v.heightDigest = j.at("heightDigest").get<std::string>();
        v.material = Ref(j.at("material"), false);
        v.navigation = Ref(j.at("navigation"), true);
        if (!j.at("tiles").is_array() || j.at("tiles").size() > 4096)
            throw std::invalid_argument("Terrain tile budget exceeded");
        for (const auto& t : j.at("tiles")) {
            AuthorJson::Fields(t, {"x", "z", "lod", "mesh", "collision", "min", "max"});
            TerrainTileRecord record;
            const auto x = AuthorJson::Unsigned(t.at("x")), z = AuthorJson::Unsigned(t.at("z")),
                       lod = AuthorJson::Unsigned(t.at("lod"));
            if (x > UINT32_MAX || z > UINT32_MAX || lod > UINT32_MAX)
                throw std::invalid_argument("Terrain tile key overflow");
            record.x = uint32_t(x);
            record.z = uint32_t(z);
            record.lod = uint32_t(lod);
            record.mesh = Ref(t.at("mesh"), false);
            record.collision = Ref(t.at("collision"), true);
            record.bounds = {AuthorJson::Vec3(t.at("min")), AuthorJson::Vec3(t.at("max")), false};
            v.tiles.push_back(std::move(record));
        }
        if (!ValidMetadata(v, error))
            return false;
        output = std::move(v);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteTerrainAsset(const TerrainAsset& v, std::string& output, std::string& error) try {
        if (!ValidMetadata(v, error))
            return false;
        Json j{{"schemaVersion", 1},
               {"settings", Settings(v.settings)},
               {"heightDigest", v.heightDigest},
               {"material", Ref(v.material)},
               {"navigation", Ref(v.navigation)},
               {"tiles", Json::array()}};
        for (const auto& t : v.tiles)
            j["tiles"].push_back({{"x", t.x},
                                  {"z", t.z},
                                  {"lod", t.lod},
                                  {"mesh", Ref(t.mesh)},
                                  {"collision", Ref(t.collision)},
                                  {"min", AuthorJson::Array(t.bounds.minimum)},
                                  {"max", AuthorJson::Array(t.bounds.maximum)}});
        output = j.dump();
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool RegisterTerrainAssetLoader(AssetManager& manager, std::string& error) {
        AssetLoader loader;
        if (!AssetType::TryParse("terrain", loader.type))
            return false;
        loader.cppType = typeid(TerrainAsset);
        loader.deferredDependencies = true;
        loader.decode = [](const AssetContainer& c, const AssetDecodeContext& ctx, AssetLoadResult& output,
                           std::string& error) {
            if (c.payloadVersion != 1 || c.payload.size() > ctx.maxResidentBytes / 8 ||
                ctx.cancellation.IsCancellationRequested() || !ctx.readDeferred) {
                error = "Terrain decode lacks fixed deferred IO or exceeds budget";
                return false;
            }
            auto v = std::make_shared<TerrainAsset>();
            if (!ReadTerrainAsset({reinterpret_cast<const char*>(c.payload.data()), c.payload.size()}, *v,
                                  error))
                return false;
            std::set<AssetId> used;
            const auto bind = [&](AssetDependency& target, bool optional) {
                if (!target.id.HasValue())
                    return optional;
                const auto found =
                    std::find_if(ctx.declaredDependencies.begin(), ctx.declaredDependencies.end(),
                                 [&](const auto& d) { return d.id == target.id; });
                if (found == ctx.declaredDependencies.end() || !found->version ||
                    found->contentHash.size() != 64 || (target.version && target.version != found->version))
                    return false;
                target = *found;
                used.insert(target.id);
                return true;
            };
            if (!bind(v->material, false) || !bind(v->navigation, !v->settings.buildNavigation)) {
                error = "Terrain material/navigation exact version mismatch";
                return false;
            }
            for (auto& tile : v->tiles)
                if (!bind(tile.mesh, false) || !bind(tile.collision, !v->settings.buildCollision)) {
                    error = "Terrain tile/collision exact version mismatch";
                    return false;
                }
            if (used.size() != ctx.declaredDependencies.size()) {
                error = "Terrain contains an unused or duplicate dependency";
                return false;
            }
            v->readDeferred = ctx.readDeferred;
            size_t resident = sizeof(TerrainAsset) + v->tiles.capacity() * sizeof(TerrainTileRecord) +
                              v->heightDigest.capacity();
            for (const auto& t : v->tiles)
                resident += t.mesh.contentHash.capacity() + t.collision.contentHash.capacity();
            if (resident > ctx.maxResidentBytes) {
                error = "Terrain metadata exceeds resident limit";
                return false;
            }
            output = MakeAssetLoadResult<TerrainAsset>(v, resident);
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
} // namespace Catalpa
