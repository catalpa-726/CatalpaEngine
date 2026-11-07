#include "catpch.h"
#include "Scene/PrefabInstantiation.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Assets/AssetManager.h"
#include "Scene/PrefabAuthorData.h"
#include "Scene/DomainComponents.h"
#include "Core/PlatformServices.h"
#include "Lighting/EnvironmentAsset.h"
#include "Scripting/ScriptAsset.h"
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa {
    namespace {
        void RemapValues(ScriptValues& values, const std::map<PersistentEntityId, PersistentEntityId>& ids) {
            for (auto& [key, value] : values)
                if (auto* id = std::get_if<PersistentEntityId>(&value)) {
                    const auto it = ids.find(*id);
                    if (it != ids.end()) *id = it->second;
                }
        }

        bool Expand(AssetId source, uint64_t version, const PrefabAsset& asset, const PrefabResolver& resolve,
                    PersistentEntityId parent, const std::map<PersistentEntityId, PersistentEntityId>& reuse,
                    std::set<AssetId>& chain, SceneAsset& expanded, PersistentEntityId& root, std::string& error) {
            if (!chain.insert(source).second || chain.size() > 32) {
                error = "Nested prefab cycle or depth limit at " + source.ToString();
                return false;
            }
            std::string checked;
            if (!WritePrefab(asset, checked, error)) return false;
            if (asset.scene.entities.size() > 65536 - expanded.entities.size()) {
                error = "Expanded prefab exceeds 65536 entities";
                return false;
            }
            std::map<PersistentEntityId, PersistentEntityId> mapping;
            for (const auto& n : asset.scene.entities) {
                PersistentEntityId id;
                if (const auto found = reuse.find(n.id); found != reuse.end()) id = found->second;
                else if (!GeneratePersistentEntityId(id, error)) return false;
                mapping.emplace(n.id, id);
                if (!n.parent.HasValue()) root = id;
            }
            for (const auto& n : asset.scene.entities) {
                auto copy = n;
                copy.id = mapping.at(n.id);
                copy.parent = n.parent.HasValue() ? mapping.at(n.parent) : parent;
                if (copy.script) RemapValues(copy.script->properties, mapping);
                if (copy.behavior) RemapValues(copy.behavior->variables, mapping);
                if (!RemapDomainEntityReferences(copy.dynamicComponents, mapping, error))
                    return false;
                copy.prefab.reset();
                if (copy.id == root) {
                    AuthorJson::Json baseline = AuthorJson::Json::object();
                    AuthorJson::Json parents = AuthorJson::Json::object();
                    for (const auto& original : asset.scene.entities)
                    {
                        baseline[original.id.ToString()] = PrefabDetail::EntityText(original);
                        parents[original.id.ToString()] = original.parent.HasValue() ? AuthorJson::Json(original.parent.ToString()) : AuthorJson::Json();
                    }
                    copy.prefab = PrefabInstanceComponent{source, version, mapping,
                        AuthorJson::Json{{"baseline", baseline}, {"parents", parents},
                            {"attachment", parent.HasValue() ? AuthorJson::Json(parent.ToString()) : AuthorJson::Json()},
                            {"values", AuthorJson::Json::object()}}.dump()};
                }
                expanded.entities.push_back(std::move(copy));
            }
            for (const auto& nested : asset.nested) {
                std::shared_ptr<const PrefabAsset> child;
                uint64_t childVersion = 0;
                if (const auto fixed = asset.nestedVersions.find(nested.asset); fixed != asset.nestedVersions.end()) {
                    child = fixed->second.GetShared();
                    childVersion = fixed->second.GetVersion();
                } else if (!resolve || !resolve(nested.asset, child, childVersion, error) || !child || !childVersion) {
                    if (error.empty()) error = "Nested prefab is unavailable: " + nested.asset.ToString();
                    return false;
                }
                PersistentEntityId childRoot;
                const auto begin = expanded.entities.size();
                // 嵌套槽位具有稳定身份；排序或基础版本改变不再重新随机生成整棵子实例。
                std::map<PersistentEntityId, PersistentEntityId> nestedMapping;
                for (const auto& node : child->scene.entities) {
                    const auto key = root.ToString() + "/" + nested.id.ToString() + "/" + node.id.ToString();
                    std::array<std::byte, 32> digest;
                    if (!ComputeSha256(std::as_bytes(std::span(key.data(), key.size())), digest, error)) return false;
                    StableId stable;
                    for (size_t i = 0; i < stable.bytes.size(); ++i) stable.bytes[i] = std::to_integer<uint8_t>(digest[i]);
                    PersistentEntityId id;
                    if (!PersistentEntityId::TryParse(stable.ToString(), id)) { error = "Invalid nested identity digest"; return false; }
                    nestedMapping.emplace(node.id, id);
                }
                if (!Expand(nested.asset, childVersion, *child, resolve, mapping.at(nested.parent),
                            nestedMapping, chain, expanded, childRoot, error)) return false;
                for (size_t i = begin; i < expanded.entities.size(); ++i)
                    if (expanded.entities[i].id == childRoot) {
                        Math::Mat4 a, b, combined;
                        Math::TransformTRS placement;
                        if (!Math::ComposeTRS(nested.local, a).IsSuccess() ||
                            !Math::ComposeTRS(expanded.entities[i].local, b).IsSuccess() ||
                            !Math::ComposeWorld(a, b, combined).IsSuccess() ||
                            !Math::DecomposeTRS(combined, placement).IsSuccess()) {
                            error = "Nested prefab placement produces unsupported shear";
                            return false;
                        }
                        expanded.entities[i].local = placement;
                        auto& component = *expanded.entities[i].prefab;
                        auto info = AuthorJson::Json::parse(component.overrides);
                        for (const auto& [local, instance] : component.entities)
                            if (instance == childRoot) {
                                auto author = PrefabDetail::Localize(expanded.entities[i],component.entities); author.id = local;
                                info["baseline"][local.ToString()] = PrefabDetail::EntityText(std::move(author));
                            }
                        component.overrides = info.dump();
                    }
            }
            chain.erase(source);
            if (expanded.entities.size() > 65536) { error = "Expanded prefab exceeds 65536 entities"; return false; }
            return true;
        }
    }

    bool PrefabInstantiation::Prepare(Scene& scene, AssetId source, uint64_t version, const PrefabAsset& asset,
        AssetManager* assets, const PrefabResolver& resolve, PersistentEntityId parent,
        const std::map<PersistentEntityId, PersistentEntityId>& reuse, SceneSubtreeSnapshot& output,
        PersistentEntityId& root, std::string& error) try {
        if (!scene.CanEditNow() || !source.HasValue() || !version ||
            (parent.HasValue() && !scene.Resolve(parent).HasValue())) {
            error = "Prefab target is unavailable";
            return false;
        }
        SceneAsset expanded;
        std::set<AssetId> chain;
        PersistentEntityId candidateRoot;
        if (!Expand(source, version, asset, resolve, parent, reuse, chain, expanded, candidateRoot, error)) return false;
        const auto required = [&]<class T>(AssetId id) {
            AssetVersionRef<T> fixed;
            bool fallback = false;
            if (!assets || !assets->Acquire(id, fixed, fallback, error) || fallback) {
                if (error.empty()) error = "Prefab component dependency is unavailable: " + id.ToString();
                return false;
            }
            return true;
        };
        for (const auto& node : expanded.entities) {
            if (node.script && !required.template operator()<ScriptAsset>(node.script->script)) return false;
            if (node.behavior && !required.template operator()<BehaviorGraphAsset>(node.behavior->graph)) return false;
            if (node.environment && !required.template operator()<EnvironmentAsset>(node.environment->environment.id)) return false;
        }
        // 按父关系构造一次 RestoreForUndo 事务；仅根的父节点允许位于实例之外。
        SceneSubtreeSnapshot snapshot;
        snapshot.m_World = scene.GetWorld().GetIdentity();
        snapshot.m_ParentGeneration = scene.Resolve(parent);
        std::map<PersistentEntityId, size_t> indices;
        std::map<PersistentEntityId, std::vector<size_t>> children;
        std::vector<size_t> order;
        for (size_t i = 0; i < expanded.entities.size(); ++i) {
            const auto& node = expanded.entities[i];
            if (!indices.emplace(node.id, i).second) { error = "Duplicate expanded prefab identity"; return false; }
            children[node.parent].push_back(i);
        }
        order = children[parent];
        for (size_t i = 0; i < order.size(); ++i) {
            const auto& descendants = children[expanded.entities[order[i]].id];
            order.insert(order.end(), descendants.begin(), descendants.end());
            if (order.size() > expanded.entities.size()) { error = "Prefab hierarchy cycle"; return false; }
        }
        if (order.size() != expanded.entities.size()) { error = "Expanded prefab has unresolved parents"; return false; }
        for (auto index : order) {
            const auto& n = expanded.entities[index];
            SceneSubtreeSnapshot::Node value;
            value.identity = {n.id, n.name};
            value.parent = n.parent;
            value.local = n.local;
            value.camera = n.camera;
            value.light = n.light;
            value.environment = n.environment;
            value.script = n.script;
            value.behavior = n.behavior;
            value.prefab = n.prefab;
            value.dynamicComponents = n.dynamicComponents;
            value.dynamicDependencies = n.dynamicDependencies;
            if (n.mesh) {
                if (!SceneSerializer::ResolveMesh(*n.mesh, assets, value.mesh.emplace(), error)) return false;
            }
            snapshot.m_Nodes.push_back(std::move(value));
        }
        output = std::move(snapshot);
        root = candidateRoot;
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool PrefabInstantiation::Instantiate(Scene& scene, AssetId source, uint64_t version, const PrefabAsset& asset,
        AssetManager* assets, const PrefabResolver& resolve, PersistentEntityId parent,
        PersistentEntityId& root, std::string& error) {
        SceneSubtreeSnapshot snapshot;
        PersistentEntityId candidate;
        if (!Prepare(scene, source, version, asset, assets, resolve, parent, {}, snapshot, candidate, error)) return false;
        const auto status = scene.RestoreForUndo(snapshot, scene.Resolve(parent));
        if (!status.IsSuccess()) { error = status.message; return false; }
        root = candidate;
        return true;
    }
}
