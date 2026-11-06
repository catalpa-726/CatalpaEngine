#include "catpch.h"
#include "Scene/PrefabReconciler.h"
#include "Scene/PrefabAuthorData.h"
#include "Scene/Scene.h"
#include "Log/Log.h"

namespace Catalpa {
    bool PrefabReconciler::DescribeOverrides(const Scene& scene, PersistentEntityId root,
        std::vector<std::string>& output, std::string& error) try {
        SceneAsset current;
        if (!SceneSerializer::Capture(scene, current, error)) return false;
        std::map<PersistentEntityId, const SceneEntityAsset*> entities;
        for (const auto& node : current.entities) entities.emplace(node.id, &node);
        const auto found = entities.find(root);
        if (found == entities.end() || !found->second->prefab) { error = "Select a prefab root"; return false; }
        const auto& instance = *found->second->prefab;
        const auto inverse = PrefabDetail::Inverse(instance.entities);
        const auto baseline = AuthorJson::Parse(instance.overrides).at("baseline");
        std::vector<std::string> paths;
        for (const auto& [local, id] : instance.entities) {
            const auto live = entities.find(id);
            if (live == entities.end()) { paths.push_back(local.ToString() + ": deleted entity (structural conflict)"); continue; }
            const auto value = PrefabDetail::EntityText(PrefabDetail::LocalizeWithInverse(*live->second, inverse));
            const auto patch = AuthorJson::Json::diff(baseline.at(local.ToString()), value);
            for (const auto& change : patch) {
                const auto path = change.at("path").get<std::string>();
                paths.push_back(local.ToString() + path + (PrefabDetail::AllowedPath(path) ? "" : " (unsupported conflict)"));
            }
        }
        output = std::move(paths);
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool PrefabReconciler::Prepare(Scene& scene, PersistentEntityId root, uint64_t version,
        const PrefabAsset& asset, AssetManager* assets, const PrefabResolver& resolver, bool revert,
        SceneSubtreeSnapshot& output, std::string& error) try {
        SceneAsset current;
        if (!SceneSerializer::Capture(scene, current, error)) return false;
        std::map<PersistentEntityId, SceneEntityAsset> entities;
        for (const auto& node : current.entities) entities.emplace(node.id, node);
        const auto found = entities.find(root);
        if (found == entities.end() || !found->second.prefab) { error = "Select a prefab instance root"; return false; }
        const auto& old = *found->second.prefab;
        SceneSubtreeSnapshot candidate;
        PersistentEntityId prepared;
        if (!PrefabInstantiation::Prepare(scene, old.source, version, asset, assets, resolver,
            found->second.parent, old.entities, candidate, prepared, error) || prepared != root) {
            if (error.empty()) error = "Prefab update changed its root identity";
            return false;
        }
        std::map<PersistentEntityId, SceneSubtreeSnapshot::Node*> targets;
        for (auto& node : candidate.m_Nodes) targets.emplace(node.identity.persistent, &node);
        std::map<PersistentEntityId, std::vector<PersistentEntityId>> children;
        for (const auto& [id, node] : entities) children[node.parent].push_back(id);
        std::vector<PersistentEntityId> subtree{root};
        std::set<PersistentEntityId> inside{root};
        for (size_t i = 0; i < subtree.size(); ++i)
            for (auto child : children[subtree[i]]) if (inside.insert(child).second) subtree.push_back(child);

        std::set<PersistentEntityId> accounted;
        for (auto& node : candidate.m_Nodes) {
            if (!node.prefab) continue;
            const auto previous = entities.find(node.identity.persistent);
            if (previous == entities.end() || !previous->second.prefab) continue;
            const auto& provenance = *previous->second.prefab;
            const auto inverse = PrefabDetail::Inverse(provenance.entities);
            const auto saved = AuthorJson::Parse(provenance.overrides);
            AuthorJson::Fields(saved, {"baseline", "parents", "attachment", "values"});
            // 顶层实例可以整体重设父级；嵌套根的挂接属于基础 Prefab 的结构。
            // 未记录旧挂接时拒绝猜测，避免基础更新静默覆盖实例内的重挂接。
            if (node.identity.persistent != root) {
                if (!saved.contains("attachment") || saved["attachment"].is_null()) {
                    error = "Nested prefab attachment baseline is missing; resolve its structure before updating";
                    return false;
                }
                const auto attachment = AuthorJson::Identity<PersistentEntityId>(saved["attachment"]);
                if (previous->second.parent != attachment) {
                    error = "Nested prefab parent override is unsupported: " + node.identity.persistent.ToString();
                    return false;
                }
            }
            auto next = AuthorJson::Parse(node.prefab->overrides);
            for (const auto& [local, instance] : provenance.entities) {
                const auto live = entities.find(instance);
                if (live == entities.end()) { error = "Prefab entity was deleted; structural override requires explicit resolution"; return false; }
                accounted.insert(instance);
                if(saved.contains("parents") && saved["parents"].contains(local.ToString()) && !saved["parents"][local.ToString()].is_null()) {
                    const auto parent=AuthorJson::Identity<PersistentEntityId>(saved["parents"][local.ToString()]);
                    if(!provenance.entities.contains(parent) || live->second.parent!=provenance.entities.at(parent)) {
                        error="Prefab parent override is unsupported: "+local.ToString();return false;
                    }
                }
                auto author = PrefabDetail::LocalizeWithInverse(live->second, inverse);
                author.id = local;
                const auto actual = PrefabDetail::EntityText(std::move(author));
                if (!saved.contains("baseline") || !saved["baseline"].contains(local.ToString())) {
                    error = "Prefab baseline is missing; update cannot safely infer overrides"; return false;
                }
                const auto patch = AuthorJson::Json::diff(saved["baseline"][local.ToString()], actual);
                for (const auto& change : patch)
                    if (!PrefabDetail::AllowedPath(change.at("path").get<std::string>())) {
                        error = "Unsupported prefab override at " + local.ToString() + change.at("path").get<std::string>();
                        return false;
                    }
                if (!next["baseline"].contains(local.ToString())) {
                    if (!patch.empty() && !revert) { error = "Base prefab removed an overridden entity: " + local.ToString(); return false; }
                    continue;
                }
                if (revert || patch.empty()) continue;
                next["values"][local.ToString()] = patch;
                const auto merged = next["baseline"][local.ToString()].patch(patch);
                SceneAsset validated;
                const AuthorJson::Json wrapper{{"schemaVersion", 1}, {"name", ""}, {"entities", AuthorJson::Json::array({merged})}};
                if (!SceneSerializer::Read(wrapper.dump(), validated, error)) return false;
                const auto& value = validated.entities.front();
                const auto target = node.prefab->entities.at(local);
                const auto to = targets.find(target);
                if (to == targets.end()) { error = "Prefab override target is missing"; return false; }
                to->second->identity.name = value.name;
                to->second->local = value.local;
                if (to->second->mesh && value.mesh) {
                    to->second->mesh->color = value.mesh->color;
                    to->second->mesh->visible = value.mesh->visible;
                }
            }
            node.prefab->overrides = next.dump();
        }
        // 不吞掉附加到实例内的作者节点，也不隐式接受外部重设父关系。
        for(const auto& [id,value]:entities) if(value.prefab && inside.contains(id) && !targets.contains(id)) {
            const auto saved = AuthorJson::Parse(value.prefab->overrides);
            const auto baseline = saved.at("baseline");
            if (!saved.contains("attachment") || saved["attachment"].is_null() ||
                value.parent != AuthorJson::Identity<PersistentEntityId>(saved["attachment"])) {
                error = "Removed nested prefab has a modified or unknown attachment";
                return false;
            }
            const auto inverse = PrefabDetail::Inverse(value.prefab->entities);
            for(const auto& [local,instance]:value.prefab->entities) {
                const auto actual=entities.find(instance);
                if(actual==entities.end()) {error="Removed nested prefab has a deleted instance entity";return false;}
                const auto& parent = saved.at("parents").at(local.ToString());
                if (!parent.is_null()) {
                    const auto parentLocal = AuthorJson::Identity<PersistentEntityId>(parent);
                    const auto expectedParent = value.prefab->entities.find(parentLocal);
                    if (expectedParent == value.prefab->entities.end() || actual->second.parent != expectedParent->second) {
                        error = "Removed nested prefab has a parent override";
                        return false;
                    }
                }
                const auto text=PrefabDetail::EntityText(PrefabDetail::LocalizeWithInverse(actual->second, inverse));
                if(!revert && baseline.at(local.ToString())!=text) {error="Base removed a modified nested prefab";return false;}
                accounted.insert(instance);
            }
        }
        for (auto id : subtree)
            if (!accounted.contains(id)) { error = "Instance contains a structural addition; detach or resolve it before updating"; return false; }
        output = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool PrefabReconciler::Replace(Scene& scene, PersistentEntityId root, const SceneSubtreeSnapshot& candidate,
        SceneSubtreeSnapshot& previous, const SceneSubtreeSnapshot* expected, std::string& error) try {
        SceneSubtreeSnapshot old;
        auto status = scene.CaptureForUndo(scene.Resolve(root), old);
        if (!status.IsSuccess()) { error = status.message; return false; }
        if(candidate.m_World!=old.m_World || candidate.m_Nodes.empty() || candidate.m_Nodes.front().identity.persistent!=root ||
            candidate.m_Nodes.front().parent!=old.m_Nodes.front().parent) {error="Prefab replacement changes its world/root/parent contract";return false;}
        if(expected) {
            if(expected->m_World!=old.m_World || expected->m_Nodes.size()!=old.m_Nodes.size()) {error="Prefab subtree changed outside its command";return false;}
            std::map<PersistentEntityId, const SceneSubtreeSnapshot::Node*> expectedNodes;
            for (const auto& node : expected->m_Nodes) expectedNodes.emplace(node.identity.persistent, &node);
            for(const auto& node:old.m_Nodes) {
                const auto found = expectedNodes.find(node.identity.persistent);
                if(found==expectedNodes.end() || !node.SameAuthorData(*found->second)) {error="Prefab author values changed outside its command";return false;}
            }
        }
        auto mapping=scene.m_Entities;
        std::vector<EntityId> removed,created;
        removed.reserve(old.m_Nodes.size());created.reserve(candidate.m_Nodes.size());
        for(const auto& node:old.m_Nodes) {removed.push_back(scene.Resolve(node.identity.persistent));mapping.erase(node.identity.persistent);}
        for(const auto& node:candidate.m_Nodes) if(mapping.contains(node.identity.persistent)) {error="Prefab replacement reuses an outside entity identity";return false;}
        struct Staging {
            World& world;std::vector<EntityId>& created;bool committed=false;
            ~Staging() {if(!committed) for(auto entity:created) {
                if(!world.Destroy(entity).IsSuccess()) std::terminate();
            }}
        } staging{scene.m_World,created};
        for(const auto& node:candidate.m_Nodes) {
            EntityId entity;
            status=scene.m_World.Create(entity);
            if(!status.IsSuccess()) {error=status.message;return false;}
            created.push_back(entity);
            const auto parent=node.parent.HasValue()?mapping.find(node.parent):mapping.end();
            if(node.parent.HasValue() && parent==mapping.end()) {error="Prefab replacement has an unresolved parent";return false;}
            status=scene.m_World.Add(entity,node.identity);
            if(status.IsSuccess()) status=scene.m_World.Add(entity,TransformComponent{node.local,Math::Mat4{1},false});
            if(status.IsSuccess()) status=scene.m_World.Add(entity,RelationshipComponent{parent==mapping.end()?EntityId{}:parent->second});
            const auto add=[&]<class T>(const std::optional<T>& value) {if(status.IsSuccess() && value)status=scene.m_World.Add(entity,*value);};
            add(node.camera);add(node.mesh);add(node.light);add(node.environment);add(node.script);add(node.behavior);add(node.prefab);
            if(!status.IsSuccess()) {error=status.message;return false;}
            if(!mapping.emplace(node.identity.persistent,entity).second) {error="Duplicate prefab replacement identity";return false;}
        }
        // 所有可能分配的复制均已成功。已捕获的已知组件只含 noexcept 析构字段，
        // 删除旧实体和交换身份表不分配；候选不会在这之前进入 Scene 的解析表。
        for(auto entity:removed) if(!scene.m_World.Destroy(entity).IsSuccess()) {
            CAT_CORE_CRITICAL("Validated prefab entity could not be retired at the commit boundary.");
            std::terminate();
        }
        scene.m_Entities.swap(mapping);
        scene.m_Dirty=true;
        staging.committed=true;
        previous = std::move(old);
        error.clear();
        return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}
