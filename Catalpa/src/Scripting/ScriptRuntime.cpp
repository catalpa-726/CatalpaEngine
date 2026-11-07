#include "catpch.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
#include "Assets/AssetManager.h"
#include "Core/PlatformServices.h"

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
        bool ValidateBackendState(IScriptBackend& backend, std::string& error) {
            uint32_t schema = 0;
            ScriptValues values;
            return backend.ExportState(schema, values, error);
        }
    }

    ScriptRuntime::ScriptRuntime()
        : m_Owner(std::this_thread::get_id()), m_Memory(std::make_shared<ScriptMemoryBudget>()) {
        m_Tasks.reserve(64);
        m_SceneRequests.reserve(8);
    }
    ScriptRuntime::~ScriptRuntime() { Stop(); }

    ScriptApi ScriptRuntime::MakeApi(Scene& scene, EntityReference entity, bool reload) {
        ScriptApi api{scene, entity, {}};
        api.reloading = reload;
        if (m_Jobs && m_Jobs->IsRunning())
            api.reserveAsync = [this]() {
                if (m_Tasks.size() >= 64 || m_NextTask >= INT64_MAX)
                    return uint64_t(0);
                return m_NextTask++;
            };
        return api;
    }
    bool ScriptRuntime::QueueEvent(EntityReference target, uint64_t generation, std::string_view channel,
                                   const ScriptValues& values, std::string& error) {
        EventValue event;
        if (!PrepareEvent(target, generation, channel, values, event, error))
            return false;
        m_Events.push_back(std::move(event));
        error.clear();
        return true;
    }
    bool ScriptRuntime::PrepareEvent(EntityReference target, uint64_t generation, std::string_view channel,
                                     const ScriptValues& values, EventValue& output, std::string& error) {
        if (!target.HasValue() || target.world != m_Scene || channel.empty() || channel.size() > 128 ||
            channel.front() == '@' || channel.find('\0') != channel.npos || m_Events.size() >= 1024 ||
            !ValidateScriptValues(values, error)) {
            if (error.empty())
                error = "Script event owner/topic/payload/capacity rejected";
            return false;
        }
        size_t bytes = 512 + channel.size();
        for (const auto& [name, value] : values) {
            bytes += name.size() + sizeof(ScriptValue);
            if (const auto* text = std::get_if<std::string>(&value))
                bytes += text->size();
        }
        if (bytes > 65536) {
            error = "Script event exceeds its 64 KiB value budget";
            return false;
        }
        EventValue event;
        event.target = target;
        event.generation = generation;
        event.channel = channel;
        event.values = values;
        if (!m_EventBudget.TryReserve(bytes, event.reservation)) {
            error = "Script inbox byte budget exceeded";
            return false;
        }
        output = std::move(event);
        error.clear();
        return true;
    }
    bool ScriptRuntime::EmitEvent(EntityReference target, std::string_view channel,
                                  const ScriptValues& values, std::string& error) {
        if (std::this_thread::get_id() != m_Owner) {
            error = "Script event requires Runtime owner thread";
            return false;
        }
        const auto found = m_Instances.find(target.persistent);
        return QueueEvent(target, found == m_Instances.end() ? 0 : found->second.generation, channel, values,
                          error);
    }
    void ScriptRuntime::CancelTasks(EntityReference target, uint64_t generation) {
        for (auto it = m_Tasks.begin(); it != m_Tasks.end();) {
            if (it->target != target || (generation && it->generation != generation)) {
                ++it;
                continue;
            }
            it->job.Cancel();
            if (m_Jobs) {
                std::string ignored;
                m_Jobs->Wait(it->job, ignored);
            }
            it = m_Tasks.erase(it);
        }
    }
    bool ScriptRuntime::PublishApi(ScriptApi& api, uint64_t generation, std::string& error) {
        if (api.asyncRequests.size() > 64 - m_Tasks.size() || api.emissions.size() > 1024 - m_Events.size() ||
            (api.requestedScene && m_SceneRequests.size() >= 8) ||
            (!api.asyncRequests.empty() && (!m_Jobs || !m_Jobs->IsRunning()))) {
            error = "Script continuation/event/scene request capacity exceeded";
            return false;
        }
        // 在世界命令发布前验证并预留事件字节；不能先移动实体，再发现同调用事件超过准入预算。
        std::list<EventValue> prepared;
        for (const auto& emission : api.emissions) {
            EventValue event;
            if (!PrepareEvent(api.self, generation, emission.channel, emission.values, event, error))
                return false;
            prepared.push_back(std::move(event));
        }
        if (api.requestedScene)
            m_SceneRequests.reserve(8);
        // worker 只处理拥有的输入值，先完成所有任务准入，再提交世界；准入失败取消候选任务。
        std::vector<Task> staged;
        staged.reserve(api.asyncRequests.size());
        struct CancelStaged {
            JobSystem* jobs;
            std::vector<Task>& tasks;
            bool published = false;
            ~CancelStaged() {
                if (published || !jobs)
                    return;
                for (auto& task : tasks)
                    task.job.Cancel();
                for (auto& task : tasks)
                    try {
                        std::string ignored;
                        jobs->Wait(task.job, ignored);
                    } catch (...) {
                    }
            }
        } cancellation{m_Jobs, staged};
        for (auto& request : api.asyncRequests) {
            auto result = std::make_shared<TaskResult>();
            Task task;
            task.ticket = request.ticket;
            task.generation = generation;
            task.target = api.self;
            task.callback = std::move(request.callback);
            task.result = result;
            task.submittedTick = m_Tick;
            const auto ticket = task.ticket;
            if (!m_Jobs->Submit(
                    [text = std::move(request.text), result, ticket](const CancellationToken& cancelled) {
                        std::array<std::byte, 32> digest;
                        std::string error;
                        const bool computed =
                            !cancelled.IsCancellationRequested() &&
                            ComputeSha256(std::as_bytes(std::span(text.data(), text.size())), digest, error);
                        ScriptValues values;
                        if (computed && !cancelled.IsCancellationRequested()) {
                            constexpr char hex[] = "0123456789abcdef";
                            std::string hash;
                            hash.reserve(64);
                            for (const auto byte : digest) {
                                const auto v = std::to_integer<unsigned>(byte);
                                hash += hex[v >> 4];
                                hash += hex[v & 15];
                            }
                            values["checksum"] = std::move(hash);
                            values["request"] = int64_t(ticket);
                        }
                        std::lock_guard lock(result->mutex);
                        result->success = computed && !cancelled.IsCancellationRequested();
                        result->values = std::move(values);
                        result->error =
                            error.empty() && !result->success ? "Continuation cancelled" : std::move(error);
                    },
                    task.job, error))
                return false;
            staged.push_back(std::move(task));
        }
        if (!api.commands.Commit(api.scene, error))
            return false;
        // 允许脚本在当前调用删除自身；此后不能再发布属于旧实体的继续点或场景请求。
        if (!api.scene.Resolve(api.self).HasValue())
            return true;
        for (const auto ticket : api.cancelledAsync) {
            for (auto it = m_Tasks.begin(); it != m_Tasks.end(); ++it)
                if (it->ticket == ticket && it->target == api.self && it->generation == generation) {
                    it->job.Cancel();
                    std::string ignored;
                    m_Jobs->Wait(it->job, ignored);
                    m_Tasks.erase(it);
                    break;
                }
            for (auto it = staged.begin(); it != staged.end(); ++it)
                if (it->ticket == ticket) {
                    it->job.Cancel();
                    std::string ignored;
                    m_Jobs->Wait(it->job, ignored);
                    staged.erase(it);
                    break;
                }
        }
        for (auto& task : staged)
            m_Tasks.push_back(std::move(task));
        cancellation.published = true;
        m_Events.splice(m_Events.end(), prepared);
        if (api.requestedScene)
            m_SceneRequests.push_back({api.self, *api.requestedScene, generation});
        return true;
    }
    std::vector<ScriptRuntime::SceneRequest> ScriptRuntime::TakeSceneRequests() {
        auto requests = std::move(m_SceneRequests);
        m_SceneRequests.clear();
        return requests;
    }
    bool ScriptRuntime::IsCurrent(EntityReference target, uint64_t generation) const noexcept {
        const auto found = m_Instances.find(target.persistent);
        return target.world == m_Scene && found != m_Instances.end() && found->second.entity == target &&
               found->second.generation == generation && !found->second.failed;
    }

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
        for (auto& task : m_Tasks)
            task.job.Cancel();
        if (m_Jobs)
            for (auto& task : m_Tasks) {
                std::string ignored;
                m_Jobs->Wait(task.job, ignored);
            }
        m_Tasks.clear();
        m_Events.clear();
        m_SceneRequests.clear();
        m_Instances.clear();
        m_Diagnostics.clear();
        m_Scene = 0;
        m_Tick = 0;
    }

    void ScriptRuntime::Tick(Scene& scene, AssetManager& assets, double seconds) {
        if (std::this_thread::get_id() != m_Owner) std::terminate();
        if (!std::isfinite(seconds) || seconds < 0) {
            m_Diagnostics = {"Script tick requires finite nonnegative simulation seconds"};
            return;
        }
        if (m_Scene != scene.GetWorld().GetIdentity()) { Stop(); m_Scene = scene.GetWorld().GetIdentity(); }
        if (m_Tick == UINT64_MAX) {
            m_Diagnostics = {"Script fixed tick exhausted"};
            return;
        }
        ++m_Tick;
        m_Diagnostics.clear();
        auto events = std::move(m_Events);
        m_Events.clear();
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
            if (removed)
                CancelTasks(entry.second.entity);
            if (removed && entry.second.backend && scene.Resolve(entry.second.entity).HasValue()) {
                ScriptApi api{scene, entry.second.entity, {}};
                std::string ignored;
                (void)entry.second.backend->Call("stop", 0, api, ignored);
            }
            return removed;
        });
        for (const auto& [id, entry] : active) {
            if (!scene.Resolve(entry.first).HasValue())
                continue;
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
                if (m_NextGeneration == UINT64_MAX) {
                    m_Diagnostics.push_back("Script instance generation exhausted");
                    continue;
                }
                auto backend = std::make_unique<LuaScriptBackend>(m_Memory);
                std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>> modules;
                std::set<AssetId> chain{entry.second.script};
                std::set<const ScriptAsset*> expanded{source.Get()};
                ScriptApi api = MakeApi(scene, entry.first, instance.backend != nullptr);
                uint32_t previousSchema = 1;
                ScriptValues previousState;
                if (instance.backend &&
                    !instance.backend->ExportState(previousSchema, previousState, error)) {
                    instance.rejectedVersion = source.GetVersion();
                    instance.rejectedAuthor = entry.second;
                    instance.diagnostic = "state export failed: " + error;
                    m_Diagnostics.push_back(id.ToString() + ": state export failed: " + error);
                    continue;
                }
                if (!ResolveModules(assets, *source.Get(), modules, chain, expanded, error)) {
                    m_Diagnostics.push_back(id.ToString() + ": " + error);
                    continue;
                }
                if (!backend->SetModules(std::move(modules), error) ||
                    !backend->Start(*source.Get(), entry.second.properties, api, error) ||
                    (instance.backend && (!backend->ImportState(previousSchema, previousState, api, error) ||
                                          !backend->Call("reload", 0, api, error))) ||
                    !ValidateBackendState(*backend, error) || !PublishApi(api, m_NextGeneration, error)) {
                    instance.rejectedVersion = source.GetVersion();
                    instance.rejectedAuthor = entry.second;
                    CancelTasks(entry.first, m_NextGeneration);
                    instance.diagnostic = error;
                    m_Diagnostics.push_back(id.ToString() + ": " + error);
                    continue;
                }
                if (instance.backend) {
                    CancelTasks(instance.entity, instance.generation);
                    ScriptApi stopping{scene, instance.entity, {}};
                    std::string ignored;
                    (void)instance.backend->Call("stop", 0, stopping, ignored);
                }
                instance.entity = entry.first;
                instance.author = entry.second;
                instance.asset = source;
                instance.backend = std::move(backend);
                instance.failed = !scene.Resolve(entry.first).HasValue();
                instance.rejectedVersion = 0;
                instance.generation = m_NextGeneration++;
                instance.diagnostic.clear();
            }
            if (!instance.backend || instance.failed) {
                if (!instance.diagnostic.empty() && m_Diagnostics.size() < 128)
                    m_Diagnostics.push_back(id.ToString() + ": " + instance.diagnostic);
                continue;
            }
            if (instance.rejectedVersion && !instance.diagnostic.empty() && m_Diagnostics.size() < 128)
                m_Diagnostics.push_back(id.ToString() + ": reload rejected: " + instance.diagnostic);
            if (!scene.Resolve(instance.entity).HasValue())
                continue;
            ScriptApi api = MakeApi(scene, instance.entity);
            if (!instance.backend->Call("update", seconds, api, error) ||
                !PublishApi(api, instance.generation, error)) {
                instance.failed = true;
                CancelTasks(instance.entity, instance.generation);
                instance.diagnostic = error;
                m_Diagnostics.push_back(id.ToString() + ": " + error);
            }
        }
        // 本 tick 开始时已经排队的事件在更新后按 FIFO 交付；回调新发出的事件留到下一 tick。
        for (auto& event : events) {
            const auto found = m_Instances.find(event.target.persistent);
            if (found == m_Instances.end() || found->second.failed || !found->second.backend ||
                found->second.entity != event.target || !scene.Resolve(event.target).HasValue() ||
                (event.generation && event.generation != found->second.generation))
                continue;
            auto& instance = found->second;
            auto api = MakeApi(scene, instance.entity);
            std::string error;
            if (!instance.backend->Event(event.channel, event.values, api, error) ||
                !PublishApi(api, instance.generation, error)) {
                instance.failed = true;
                CancelTasks(instance.entity, instance.generation);
                instance.diagnostic = error;
                m_Diagnostics.push_back(event.target.persistent.ToString() + ": event failed: " + error);
            }
        }
        for (size_t index = 0; index < m_Tasks.size();) {
            if (m_Tasks[index].submittedTick >= m_Tick || !m_Jobs ||
                !m_Jobs->IsComplete(m_Tasks[index].job)) {
                ++index;
                continue;
            }
            auto task = std::move(m_Tasks[index]);
            m_Tasks.erase(m_Tasks.begin() + index);
            const auto found = m_Instances.find(task.target.persistent);
            if (found == m_Instances.end() || found->second.failed ||
                found->second.generation != task.generation || found->second.entity != task.target ||
                !scene.Resolve(task.target).HasValue())
                continue;
            ScriptValues values;
            std::string error;
            bool success = false;
            {
                std::lock_guard lock(task.result->mutex);
                success = task.result->success;
                values = std::move(task.result->values);
                error = task.result->error;
            }
            if (!success) {
                if (error.empty())
                    error = task.job.GetError();
                m_Diagnostics.push_back(task.target.persistent.ToString() + ": async failed: " + error);
                continue;
            }
            auto& instance = found->second;
            auto api = MakeApi(scene, instance.entity);
            if (!instance.backend->Event("@continuation/" + task.callback, values, api, error) ||
                !PublishApi(api, instance.generation, error)) {
                instance.failed = true;
                CancelTasks(instance.entity, instance.generation);
                instance.diagnostic = error;
                m_Diagnostics.push_back(task.target.persistent.ToString() +
                                        ": continuation failed: " + error);
            }
        }
        for (auto& [id, instance] : m_Instances)
            if (!scene.Resolve(instance.entity).HasValue()) {
                instance.failed = true;
                CancelTasks(instance.entity, instance.generation);
            }
    }
}
