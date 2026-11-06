#include "catpch.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
    namespace {
        bool ResolveModules(AssetManager& assets, const ScriptAsset& source,
            std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>>& modules,
            std::set<AssetId>& chain, std::set<const ScriptAsset*>& expanded, std::string& error) {
            if (chain.size() > 32) { error = "Script module depth exceeded"; return false; }
            for (const auto& [name, id] : source.modules) {
                if (!chain.insert(id).second) { error = "Script module dependency cycle: " + name; return false; }
                AssetVersionRef<ScriptAsset> module;
                bool fallback = false;
                if (const auto fixed=source.moduleVersions.find(name);fixed!=source.moduleVersions.end()) module=fixed->second;
                else if (!assets.Acquire(id, module, fallback, error) || fallback) {
                    (void)assets.Request(id, error);
                    return false;
                }
                const auto existing = modules.find(name);
                if (existing != modules.end() && existing->second != module.GetShared()) {
                    error = "Conflicting declared module alias: " + name;
                    return false;
                }
                modules[name] = module.GetShared();
                if (modules.size() > 256) { error = "Script module alias limit exceeded"; return false; }
                // 同一固定版本的共享子图只展开一次；活动链仍在此之前检查循环。
                if (expanded.insert(module.Get()).second &&
                    !ResolveModules(assets, *module.Get(), modules, chain, expanded, error)) return false;
                chain.erase(id);
            }
            return true;
        }
    }

    ScriptRuntime::ScriptRuntime() : m_Owner(std::this_thread::get_id()), m_Memory(std::make_shared<ScriptMemoryBudget>()) {}
    ScriptRuntime::~ScriptRuntime() { Stop(); }

    void ScriptRuntime::Stop(Scene* scene) {
        if (std::this_thread::get_id() != m_Owner) std::terminate();
        if (scene && scene->GetWorld().GetIdentity() == m_Scene)
            for (auto& [id, instance] : m_Instances)
                if (instance.backend && scene->Resolve(instance.entity).HasValue()) {
                    ScriptApi api{*scene, instance.entity, {}};
                    std::string ignored;
                    (void)instance.backend->Call("stop", 0, api, ignored);
                    // Stop 只收束实例，不在退出途中继续发布世界写入。
                }
        m_Instances.clear();
        m_Diagnostics.clear();
        m_Scene = 0;
    }

    void ScriptRuntime::Tick(Scene& scene, AssetManager& assets, double seconds) {
        if (std::this_thread::get_id() != m_Owner) std::terminate();
        if (!std::isfinite(seconds) || seconds < 0) {
            m_Diagnostics = {"Script tick requires finite nonnegative simulation seconds"};
            return;
        }
        if (m_Scene != scene.GetWorld().GetIdentity()) { Stop(); m_Scene = scene.GetWorld().GetIdentity(); }
        m_Diagnostics.clear();
        std::map<PersistentEntityId, std::pair<EntityReference, ScriptComponent>> active;
        const auto scanned = scene.GetWorld().ForEach([&](EntityId entity) -> WorldStatus {
            if (!scene.GetWorld().Has<ScriptComponent>(entity)) return {};
            ScriptComponent component;
            const auto status = scene.GetWorld().Get(entity, component);
            if (status.IsSuccess() && component.enabled) {
                const auto ref = scene.MakeReference(entity);
                active.emplace(ref.persistent, std::make_pair(ref, std::move(component)));
            }
            return status;
        });
        if (!scanned.IsSuccess()) { m_Diagnostics.push_back(scanned.message); return; }
        std::erase_if(m_Instances, [&](auto& entry) {
            const auto found = active.find(entry.first);
            const bool removed = found == active.end() || found->second.first != entry.second.entity;
            if (removed && entry.second.backend && scene.Resolve(entry.second.entity).HasValue()) {
                ScriptApi api{scene, entry.second.entity, {}};
                std::string ignored;
                (void)entry.second.backend->Call("stop", 0, api, ignored);
            }
            return removed;
        });
        for (const auto& [id, entry] : active) {
            if(!m_Instances.contains(id) && m_Instances.size()>=64) {
                m_Diagnostics.push_back("Script instance limit (64) exceeded");continue;
            }
            auto& instance = m_Instances[id];
            instance.entity = entry.first;
            AssetVersionRef<ScriptAsset> source;
            bool fallback = false;
            std::string error;
            if (!assets.Acquire(entry.second.script, source, fallback, error) || fallback) {
                (void)assets.Request(entry.second.script, error);
                if (!error.empty()) m_Diagnostics.push_back(id.ToString() + ": " + error);
                continue;
            }
            const bool changed = !instance.backend || instance.author != entry.second ||
                                 instance.asset.GetVersion() != source.GetVersion();
            if (changed && (instance.rejectedVersion != source.GetVersion() || instance.rejectedAuthor != entry.second)) {
                auto backend = std::make_unique<LuaScriptBackend>(m_Memory);
                std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>> modules;
                std::set<AssetId> chain{entry.second.script};
                std::set<const ScriptAsset*> expanded{source.Get()};
                ScriptApi api{scene, entry.first, {}};
                if (!ResolveModules(assets, *source.Get(), modules, chain, expanded, error)) {
                    m_Diagnostics.push_back(id.ToString() + ": " + error);
                    continue;
                }
                if (!backend->SetModules(std::move(modules), error) ||
                    !backend->Start(*source.Get(), entry.second.properties, api, error) ||
                    !api.commands.Commit(scene, error)) {
                    instance.rejectedVersion = source.GetVersion();
                    instance.rejectedAuthor = entry.second;
                    m_Diagnostics.push_back(id.ToString() + ": " + error);
                    continue;
                }
                if (instance.backend) {
                    ScriptApi stopping{scene, instance.entity, {}};
                    std::string ignored;
                    (void)instance.backend->Call("stop", 0, stopping, ignored);
                }
                instance.entity = entry.first;
                instance.author = entry.second;
                instance.asset = source;
                instance.backend = std::move(backend);
                instance.failed = false;
                instance.rejectedVersion = 0;
            }
            if (!instance.backend || instance.failed) continue;
            ScriptApi api{scene, instance.entity, {}};
            if (!instance.backend->Call("update", seconds, api, error) || !api.commands.Commit(scene, error)) {
                instance.failed = true;
                m_Diagnostics.push_back(id.ToString() + ": " + error);
            }
        }
    }
}
