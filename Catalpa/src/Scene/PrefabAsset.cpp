#include "catpch.h"
#include "Scene/PrefabAsset.h"
#include "Scene/SceneSerializer.h"
#include "Core/Serialization/AuthorJson.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
    bool ReadPrefab(std::string_view text, PrefabAsset& output, std::string& error) try {
        using namespace AuthorJson;
        const auto doc = Parse(text);
        Fields(doc, {"schemaVersion", "scene", "nested"});
        if (Unsigned(doc.at("schemaVersion")) != 1) throw std::invalid_argument("Unsupported prefab version");
        PrefabAsset asset;
        if (!SceneSerializer::Read(doc.at("scene").dump(), asset.scene, error)) return false;
        const auto roots = std::count_if(asset.scene.entities.begin(), asset.scene.entities.end(),
                                        [](const auto& n) { return !n.parent.HasValue(); });
        if (roots != 1) throw std::invalid_argument("Prefab requires one root entity");
        for(const auto& entity:asset.scene.entities)
            if(entity.prefab) throw std::invalid_argument("Prefab sources declare nesting through stable slots, not instance provenance");
        std::set<PersistentEntityId> localIds;
        for (const auto& entity : asset.scene.entities) localIds.insert(entity.id);
        for (const auto& entity : asset.scene.entities) if (entity.script)
            for (const auto& [name, value] : entity.script->properties)
                if (const auto* target = std::get_if<PersistentEntityId>(&value); target && !localIds.contains(*target))
                    throw std::invalid_argument("Prefab entity reference must be local: " + name);
        if (!doc.at("nested").is_array() || doc.at("nested").size() > 256)
            throw std::invalid_argument("Invalid nested prefab list");
        std::set<StableId> slots;
        for (const auto& v : doc.at("nested")) {
            Fields(v, {"id", "asset", "parent", "translation", "rotation", "scale"});
            PrefabNest nest;
            nest.id = Identity<StableId>(v.at("id"));
            if (!slots.insert(nest.id).second) throw std::invalid_argument("Duplicate prefab slot identity");
            nest.asset = Identity<AssetId>(v.at("asset"));
            nest.parent = Identity<PersistentEntityId>(v.at("parent"));
            if (std::none_of(asset.scene.entities.begin(), asset.scene.entities.end(),
                             [&](const auto& n) { return n.id == nest.parent; }))
                throw std::invalid_argument("Nested prefab parent is missing");
            nest.local.translation = Vec3(v.at("translation"));
            nest.local.scale = Vec3(v.at("scale"));
            const auto& q = v.at("rotation");
            if (!q.is_array() || q.size() != 4) throw std::invalid_argument("Invalid nested rotation");
            nest.local.rotation = {float(Number(q[0])), float(Number(q[1])), float(Number(q[2])), float(Number(q[3]))};
            if (!Math::ValidateTRS(nest.local).IsSuccess()) throw std::invalid_argument("Invalid nested TRS");
            asset.nested.push_back(std::move(nest));
        }
        output = std::move(asset);
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool WritePrefab(const PrefabAsset& asset, std::string& output, std::string& error) try {
        using namespace AuthorJson;
        std::string scene;
        if (!SceneSerializer::Write(asset.scene, scene, error)) return false;
        Json doc{{"schemaVersion", 1}, {"scene", Json::parse(scene)}, {"nested", Json::array()}};
        for (const auto& n : asset.nested)
            doc["nested"].push_back({{"id", n.id.ToString()}, {"asset", n.asset.ToString()}, {"parent", n.parent.ToString()},
                {"translation", Array(n.local.translation)}, {"scale", Array(n.local.scale)},
                {"rotation", Json::array({n.local.rotation.w, n.local.rotation.x, n.local.rotation.y, n.local.rotation.z})}});
        PrefabAsset checked;
        const auto text = doc.dump(2);
        if (!ReadPrefab(text, checked, error)) return false;
        output = text;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool RegisterPrefabAssetLoader(AssetManager& manager, std::string& error) {
        AssetLoader loader;
        if (!AssetType::TryParse("prefab", loader.type)) return false;
        loader.cppType = typeid(PrefabAsset);
        loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context,
                           AssetLoadResult& output, std::string& error) {
            if (c.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
                c.payload.size() > context.maxResidentBytes) {
                error = "Prefab payload version, budget or cancellation";
                return false;
            }
            auto value = std::make_shared<PrefabAsset>();
            if (!ReadPrefab(
                    std::string_view(reinterpret_cast<const char*>(c.payload.data()), c.payload.size()),
                    *value, error))
                return false;
            if (!SceneSerializer::BindDependencies(value->scene, context, error))
                return false;
            for (const auto& nest : value->nested) {
                const auto dependency = std::find_if(context.dependencies.begin(), context.dependencies.end(),
                    [&](const auto& d) { return d.id == nest.asset && d.cppType == typeid(PrefabAsset); });
                if (dependency == context.dependencies.end()) {
                    error = "Nested prefab dependency is missing: " + nest.asset.ToString();
                    return false;
                }
                AssetVersionRef<PrefabAsset> fixed;
                if (!AssetVersionRef<PrefabAsset>::TryCreate(
                        nest.asset, dependency->version,
                        std::static_pointer_cast<const PrefabAsset>(dependency->value), fixed, error))
                    return false;
                value->nestedVersions.insert_or_assign(nest.asset, std::move(fixed));
            }
            const size_t resident =
                EstimateSceneAssetResidentBytes(value->scene) + sizeof(PrefabAsset) - sizeof(SceneAsset) +
                value->nested.capacity() * sizeof(PrefabNest) + 4 * sizeof(void*) +
                value->nestedVersions.size() *
                    (sizeof(decltype(value->nestedVersions)::value_type) + 4 * sizeof(void*));
            if (resident > context.maxResidentBytes) {
                error = "Prefab domain tree/fixed dependency budget";
                return false;
            }
            output = MakeAssetLoadResult<PrefabAsset>(value, resident);
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
}
