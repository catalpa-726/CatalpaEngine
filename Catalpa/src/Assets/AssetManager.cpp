#include "catpch.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetDependencyGraph.h"
#include "Log/Log.h"
namespace Catalpa {
    namespace {
        // 父资产持有解码时的确切依赖版本。即使父记录未改，依赖移动或换版也须重新验证。
        std::set<AssetId> ChangedAssets(const AssetRegistry& previous, const AssetRegistry& next) {
            std::map<AssetId, std::vector<AssetId>> dependents;
            std::set<AssetId> affected;
            for (const auto& record : next.GetRecords()) {
                for (auto dependency : record.dependencies) dependents[dependency].push_back(record.id);
                const auto* old = previous.Find(record.id);
                if (!old || old->type != record.type || old->version != record.version ||
                    old->contentHash != record.contentHash || old->dependencies != record.dependencies ||
                    old->locator.kind != record.locator.kind || old->locator.path != record.locator.path) {
                    affected.insert(record.id);
                }
            }
            for (const auto& record : previous.GetRecords()) {
                if (!next.Find(record.id)) affected.insert(record.id);
            }
            std::vector<AssetId> queue(affected.begin(), affected.end());
            for (size_t i = 0; i < queue.size(); ++i) {
                const auto found = dependents.find(queue[i]);
                if (found == dependents.end()) continue;
                for (auto parent : found->second) {
                    if (affected.insert(parent).second) queue.push_back(parent);
                }
            }
            return affected;
        }
    }
    struct AssetManager::Impl {
        struct Retained {
            AssetLoadResult decoded;
            MemoryReservation reservation;
            std::vector<AssetDependencyValue> dependencies;
            Retained(AssetLoadResult value, MemoryReservation bytes, std::vector<AssetDependencyValue> fixed)
                : decoded(std::move(value)), reservation(std::move(bytes)), dependencies(std::move(fixed)) {}
        };
        struct Entry {
            AssetType type;
            AssetStatus status = AssetStatus::Unloaded;
            uint64_t generation = 0;
            AssetSnapshot current;
            std::string diagnostic, contentHash;
            JobHandle job;
            bool builtin = false;
        };
        struct Completion { AssetLoadResult result; std::vector<AssetDependencyValue> dependencies; std::string error, contentHash; bool success = false, cancelled = false; };
        struct Pending {
            AssetId id; uint64_t generation = 0; JobHandle job;
            std::shared_ptr<Completion> result;
            MemoryReservation reservation;
        };
        JobSystem& jobs;
        VirtualFileSystem files;
        AssetRegistry registry;
        AssetManagerConfig config;
        MemoryBudget budget;
        std::thread::id owner = std::this_thread::get_id();
        std::map<std::string, AssetLoader, std::less<>> loaders;
        std::map<AssetId, Entry> entries;
        std::map<std::pair<AssetId, uint64_t>, std::string> knownDigests;
        std::map<std::string, std::filesystem::path, std::less<>> installedRoots;
        std::vector<Pending> pending;
        bool closed = false;
        Impl(JobSystem& j, VirtualFileSystem f, AssetRegistry r, AssetManagerConfig c)
            : jobs(j), files(std::move(f)), registry(std::move(r)), config(c), budget(c.memoryBudgetBytes) {
            if (c.maxPending == 0 || c.maxPending > 4096 || c.maxKnownVersions == 0 || c.maxKnownVersions > 65536 || c.maxFileBytes == 0 || c.maxResidentBytes == 0 ||
                c.maxFileBytes > (std::numeric_limits<size_t>::max() - c.maxResidentBytes) / 2)
                throw std::invalid_argument("Invalid bounded asset manager configuration");
            pending.reserve(c.maxPending);
        }
        bool Owner(std::string& error) const {
            if (closed || owner != std::this_thread::get_id()) { error = "Asset operation requires an open manager on its owner thread"; return false; }
            return true;
        }
        const AssetLoader* Loader(std::type_index type) const {
            for (const auto& [key, loader] : loaders) { (void)key; if (loader.cppType == type) return &loader; }
            return nullptr;
        }
        AssetLoadResult Retain(AssetLoadResult data, MemoryReservation reservation, std::vector<AssetDependencyValue> fixed = {}) {
            auto ownerValue = std::make_shared<Retained>(std::move(data), std::move(reservation), std::move(fixed));
            auto result = ownerValue->decoded;
            result.value = std::shared_ptr<const void>(ownerValue, result.value.get());
            return result;
        }
        void Fail(Entry& entry, std::string message) { entry.status = AssetStatus::Failed; entry.diagnostic = std::move(message); }
        bool Begin(AssetId id, std::set<AssetId>& stack, std::string& error, bool force) {
            const auto* record = registry.Find(id);
            if (!record) { error = "Asset is not registered"; return false; }
            if (stack.contains(id)) { error = "Asset dependency cycle at " + id.ToString(); return false; }
            if (stack.size() >= 128) { error = "Asset dependency depth limit at " + id.ToString(); return false; }
            stack.insert(id);
            auto& entry = entries[id]; entry.type = record->type;
            if (!force && (entry.status == AssetStatus::Loading || entry.status == AssetStatus::WaitingForDependencies ||
                (entry.status == AssetStatus::Ready && entry.current.version == record->version))) { stack.erase(id); error.clear(); return true; }
            if (entry.generation == std::numeric_limits<uint64_t>::max()) { error = "Asset request generation exhausted"; stack.erase(id); return false; }
            entry.job.Cancel(); ++entry.generation; entry.diagnostic.clear();
            if (record->locator.kind != AssetLocationKind::Cooked) { error = "Runtime asset loading requires cooked content"; Fail(entry, error); stack.erase(id); return false; }
            if (!loaders.contains(record->type.ToString())) { error = "Asset type has no registered loader"; Fail(entry, error); stack.erase(id); return false; }
            size_t waiting = pending.size();
            for (const auto& [key, value] : entries) { (void)key; if (value.status == AssetStatus::WaitingForDependencies) ++waiting; }
            if (waiting >= config.maxPending) { error = "Asset pending capacity is exhausted"; Fail(entry, error); stack.erase(id); return false; }
            entry.status = AssetStatus::WaitingForDependencies;
            for (auto dependency : record->dependencies) {
                // 环和深度限制统一由递归入口诊断，不能短路跳过error写入。
                if (!Begin(dependency, stack, error, false)) { Fail(entry, "Dependency rejected: " + error); stack.erase(id); return false; }
            }
            stack.erase(id);
            Schedule(id, entry);
            if (entry.status == AssetStatus::Failed) { error = entry.diagnostic; return false; }
            error.clear(); return true;
        }
        void Schedule(AssetId id, Entry& entry) {
            const auto* record = registry.Find(id);
            if (!record) { Fail(entry, "Asset disappeared from registry"); return; }
            const auto known = knownDigests.find({id, record->version});
            if (known == knownDigests.end() && knownDigests.size() >= config.maxKnownVersions) { Fail(entry, "Asset content-version history capacity is exhausted"); return; }
            AssetDecodeContext context; context.maxResidentBytes = config.maxResidentBytes;
            std::vector<AssetDependency> expectedDependencies;
            for (auto dependency : record->dependencies) {
                const auto* metadata = registry.Find(dependency);
                const auto found = entries.find(dependency);
                if (!metadata || found == entries.end()) { Fail(entry, "Dependency is missing"); return; }
                const auto& loaded = found->second;
                if (loaded.status == AssetStatus::Failed || loaded.status == AssetStatus::Cancelled || loaded.status == AssetStatus::Unloaded) { Fail(entry, "Dependency failed, was cancelled or unloaded"); return; }
                if (loaded.status != AssetStatus::Ready || !loaded.current.asset.value || loaded.current.version != metadata->version) return;
                if (loaded.contentHash.empty()) { Fail(entry, "Cooked dependency has no verified container digest"); return; }
                context.dependencies.push_back({dependency, loaded.current.version, loaded.current.asset.value, loaded.current.asset.cppType});
                expectedDependencies.push_back({dependency, loaded.current.version, loaded.contentHash});
            }
            MemoryReservation reservation;
            if (!budget.TryReserve(config.maxFileBytes * 2 + config.maxResidentBytes, reservation)) { Fail(entry, "Asset memory budget is exhausted"); return; }
            auto result = std::make_shared<Completion>();
            const auto loader = loaders.at(record->type.ToString());
            const auto metadata = *record;
            const auto previousHash = known != knownDigests.end() ? known->second : std::string{};
            const auto mountedFiles = files;
            const size_t maxFile = config.maxFileBytes;
            JobHandle job; std::string error;
            if (!jobs.Submit([result, loader, metadata, previousHash, mountedFiles, maxFile, context = std::move(context), expected = std::move(expectedDependencies)](const CancellationToken& cancellation) mutable {
                context.cancellation = cancellation;
                if (cancellation.IsCancellationRequested()) { result->cancelled = true; return; }
                std::vector<std::byte> bytes;
                if (!mountedFiles.ReadFile(metadata.locator.path, maxFile, bytes, result->error)) return;
                if (!ComputeAssetContentHash(bytes, result->contentHash, result->error)) return;
                if (!metadata.contentHash.empty() && metadata.contentHash != result->contentHash) { result->error = "Container digest differs from registry"; return; }
                if (!previousHash.empty() && previousHash != result->contentHash) { result->error = "Content changed without advancing the asset version"; return; }
                AssetContainer container;
                if (!AssetContainerReader::Read(bytes, container, result->error, maxFile)) return;
                if (container.id != metadata.id || container.version != metadata.version || container.type != metadata.type) { result->error = "Container identity/type/version differs from registry"; return; }
                if (container.dependencies.size() != expected.size()) { result->error = "Container dependency table differs from registry"; return; }
                for (const auto& dependency : container.dependencies) {
                    const auto match = std::find_if(expected.begin(), expected.end(), [&](const auto& item) { return item.id == dependency.id; });
                    if (match == expected.end() || match->version != dependency.version || match->contentHash != dependency.contentHash) {
                        result->error = "Container dependency version or digest differs from loaded snapshot"; return;
                    }
                }
                if (cancellation.IsCancellationRequested()) { result->cancelled = true; return; }
                if (!loader.decode(container, context, result->result, result->error)) return;
                if (!result->result.value || result->result.cppType != loader.cppType || result->result.residentBytes == 0 || result->result.residentBytes > context.maxResidentBytes) {
                    result->error = "Loader returned an invalid type, owner or resident byte count"; result->result = {}; return;
                }
                result->cancelled = cancellation.IsCancellationRequested();
                result->dependencies = std::move(context.dependencies);
                result->success = !result->cancelled;
            }, job, error)) { Fail(entry, std::move(error)); return; }
            pending.push_back({id, entry.generation, job, std::move(result), std::move(reservation)});
            entry.job = job; entry.status = AssetStatus::Loading;
        }
    };
    AssetManager::AssetManager(JobSystem& jobs, VirtualFileSystem files, AssetRegistry registry, AssetManagerConfig config)
        : m_Impl(std::make_unique<Impl>(jobs, std::move(files), std::move(registry), config)) {}
    AssetManager::~AssetManager() { Shutdown(); }
    bool AssetManager::RegisterLoader(AssetLoader loader, std::string& error) {
        try {
            if (!m_Impl->Owner(error)) return false;
            if (!loader.type.HasValue() || loader.cppType == std::type_index(typeid(void)) || !loader.decode || m_Impl->loaders.contains(loader.type.ToString()) || m_Impl->Loader(loader.cppType)) {
                error = "Loader requires a unique stable type, runtime payload type and decoder"; return false;
            }
            if (loader.fallback.value || loader.fallbackId.HasValue()) { error = "Fallback must be published through the manager and selected with SetFallback"; return false; }
            const auto key = loader.type.ToString();
            m_Impl->loaders.emplace(key, std::move(loader)); error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool AssetManager::SetFallback(AssetId id, std::type_index type, std::string& error) {
        if (!m_Impl->Owner(error)) return false;
        const auto found = m_Impl->entries.find(id);
        if (found == m_Impl->entries.end() || found->second.status != AssetStatus::Ready || found->second.current.asset.cppType != type) {
            error = "Fallback must be a ready asset of the exact payload type"; return false;
        }
        auto loader = m_Impl->loaders.find(found->second.type.ToString());
        if (loader == m_Impl->loaders.end()) { error = "Fallback asset type is not registered"; return false; }
        loader->second.fallbackId = id; loader->second.fallbackVersion = found->second.current.version;
        loader->second.fallback = found->second.current.asset; error.clear(); return true;
    }
    bool AssetManager::ReplaceRegistry(AssetRegistry registry, std::string& error) {
        try {
            if (!m_Impl->Owner(error)) return false;
            for (auto& [id, entry] : m_Impl->entries) {
                if (entry.generation == std::numeric_limits<uint64_t>::max()) { error = "Asset request generation exhausted"; return false; }
                if (const auto* next = registry.Find(id); next) {
                    const auto known = m_Impl->knownDigests.find({id, next->version});
                    if (next->type != entry.type || entry.builtin || (known != m_Impl->knownDigests.end() && !next->contentHash.empty() && next->contentHash != known->second)) {
                        error = "Registry replacement changes an asset type, reuses a content version, or collides with a builtin"; return false;
                    }
                }
            }
            const auto affected = ChangedAssets(m_Impl->registry, registry);
            m_Impl->registry = std::move(registry);
            for (auto& [id, entry] : m_Impl->entries) {
                if (!affected.contains(id) && entry.status != AssetStatus::Failed && entry.status != AssetStatus::Cancelled) continue;
                entry.job.Cancel(); ++entry.generation;
                if (!entry.builtin) {
                    entry.status = AssetStatus::Unloaded;
                    if (!m_Impl->registry.Find(id)) {
                        // 已交付场景的快照继续由场景持有；撤下的记录不能供新调用再次获取。
                        entry.current = {};
                        entry.contentHash.clear();
                    }
                }
                entry.diagnostic.clear();
            }
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AssetManager::InstallCookedCatalog(const std::filesystem::path& absoluteRoot,
                                            const AssetRegistry& catalog, std::string& error,
                                            bool replaceMountContents) {
        try {
            if (!m_Impl->Owner(error))
                return false;
            if (!absoluteRoot.is_absolute() || catalog.GetRecords().empty()) {
                error = "Cooked catalog requires an absolute root and records";
                return false;
            }
            const auto root = std::filesystem::canonical(absoluteRoot);
            auto files = m_Impl->files;
            auto roots = m_Impl->installedRoots;
            std::map<AssetId, AssetRecord> merged;
            for (const auto& r : m_Impl->registry.GetRecords())
                merged.emplace(r.id, r);
            std::vector<AssetId> changed;
            std::set<AssetId> replaced;
            std::set<std::string> publishedMounts;
            for (const auto& r : catalog.GetRecords()) {
                publishedMounts.insert(r.locator.path.MountName());
                replaced.insert(r.id);
                const auto* old = m_Impl->registry.Find(r.id);
                if (old && (old->version != r.version || old->contentHash != r.contentHash))
                    changed.push_back(r.id);
            }
            if (replaceMountContents) {
                for (const auto& old : m_Impl->registry.GetRecords()) {
                    if (publishedMounts.contains(old.locator.path.MountName()) && !replaced.contains(old.id))
                        changed.push_back(old.id);
                }
            }
            AssetDependencyGraph oldGraph;
            if (!oldGraph.Build(m_Impl->registry.GetRecords(), error))
                return false;
            for (auto id : oldGraph.Affected(changed))
                if (!replaced.contains(id))
                    merged.erase(id);
            for (const auto& r : catalog.GetRecords()) {
                if (r.locator.kind != AssetLocationKind::Cooked || r.contentHash.empty()) {
                    error = "Catalog contains a non-cooked or unhashed asset";
                    return false;
                }
                const auto& mount = r.locator.path.MountName();
                const auto knownRoot = roots.find(mount);
                if (knownRoot == roots.end()) {
                    if (!files.MountReadOnly(mount, root, 0, error))
                        return false;
                    roots.emplace(mount, root);
                } else if (knownRoot->second != root) {
                    error = "Cooked mount already belongs to another directory";
                    return false;
                }
                const auto old = merged.find(r.id);
                if (old != merged.end() && old->second.version == r.version) {
                    if (old->second.type != r.type || old->second.contentHash != r.contentHash ||
                        old->second.dependencies != r.dependencies) {
                        error = "Cooked catalog reuses an identity/version with different content";
                        return false;
                    }
                    // 项目可打包内置着色器的同一份字节，保留引擎在启动时为它建立的挂载。
                    // 工具安装的记录则采用最新位置，项目移动后仍能重新加载同一内容版本。
                    if (r.type.ToString() == "shader" &&
                        !m_Impl->installedRoots.contains(old->second.locator.path.MountName()))
                        continue;
                }
                merged.insert_or_assign(r.id, r);
            }
            std::vector<AssetRecord> records;
            records.reserve(merged.size());
            for (auto& [id, r] : merged) {
                (void)id;
                records.push_back(std::move(r));
            }
            AssetRegistry next;
            AssetDependencyGraph graph;
            if (!next.Replace(records, error) || !graph.Build(records, error))
                return false;
            // 文件挂载在ReplaceRegistry成功后通过无分配swap发布；失败不改变当前挂载。
            if (!ReplaceRegistry(std::move(next), error))
                return false;
            m_Impl->files.Swap(files);
            m_Impl->installedRoots.swap(roots);
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
    bool AssetManager::Request(AssetId id, std::string& error, bool forceReload) {
        try { if (!m_Impl->Owner(error)) return false; std::set<AssetId> stack; return m_Impl->Begin(id, stack, error, forceReload); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    void AssetManager::Cancel(AssetId id) {
        std::string error; if (!m_Impl->Owner(error)) throw std::logic_error(error);
        const auto found = m_Impl->entries.find(id); if (found == m_Impl->entries.end()) return;
        auto& entry = found->second; entry.job.Cancel();
        if (entry.generation != std::numeric_limits<uint64_t>::max()) ++entry.generation;
        entry.status = AssetStatus::Cancelled; entry.diagnostic = "Asset request cancelled";
    }
    void AssetManager::Unload(AssetId id) {
        Cancel(id);
        const auto found = m_Impl->entries.find(id); if (found == m_Impl->entries.end()) return;
        found->second.current.asset = {}; found->second.status = AssetStatus::Unloaded;
    }
    void AssetManager::Pump() {
        std::string error; if (!m_Impl->Owner(error)) throw std::logic_error(error);
        for (auto it = m_Impl->pending.begin(); it != m_Impl->pending.end();) {
            if (!m_Impl->jobs.IsComplete(it->job)) { ++it; continue; }
            const auto found = m_Impl->entries.find(it->id);
            if (found != m_Impl->entries.end() && found->second.generation == it->generation && found->second.status == AssetStatus::Loading) {
                auto& entry = found->second; auto& completed = *it->result;
                const auto jobError = it->job.GetError();
                if (completed.cancelled) { entry.status = AssetStatus::Cancelled; entry.diagnostic = "Worker observed cancellation"; }
                else if (!completed.success || !jobError.empty()) m_Impl->Fail(entry, !jobError.empty() ? jobError : (completed.error.empty() ? "Asset decode failed" : completed.error));
                else {
                    const auto* record = m_Impl->registry.Find(it->id);
                    if (!record) m_Impl->Fail(entry, "Asset disappeared before publication");
                    else {
                        try {
                            const auto key = std::make_pair(it->id, record->version);
                            const auto known = m_Impl->knownDigests.find(key);
                            if ((known != m_Impl->knownDigests.end() && known->second != completed.contentHash) ||
                                (known == m_Impl->knownDigests.end() && m_Impl->knownDigests.size() >= m_Impl->config.maxKnownVersions)) {
                                m_Impl->Fail(entry, "Asset content key was reused or version history capacity is exhausted");
                                it = m_Impl->pending.erase(it); continue;
                            }
                            m_Impl->knownDigests.try_emplace(key, completed.contentHash);
                            it->reservation.Shrink(completed.result.residentBytes);
                            auto retained = m_Impl->Retain(std::move(completed.result), std::move(it->reservation), std::move(completed.dependencies));
                            entry.current = {it->id, record->version, std::move(retained), false};
                            entry.contentHash = std::move(completed.contentHash); entry.diagnostic.clear(); entry.status = AssetStatus::Ready;
                        } catch (const std::exception& exception) { m_Impl->Fail(entry, exception.what()); }
                    }
                }
            }
            it = m_Impl->pending.erase(it);
        }
        m_Impl->jobs.CollectCompleted();
        for (auto& [id, entry] : m_Impl->entries) if (entry.status == AssetStatus::WaitingForDependencies) {
            try { m_Impl->Schedule(id, entry); }
            catch (const std::exception& exception) { m_Impl->Fail(entry, exception.what()); }
        }
    }
    void AssetManager::Shutdown() noexcept {
        if (!m_Impl || m_Impl->closed) return;
        if (m_Impl->owner != std::this_thread::get_id()) {
            if (Log::GetCoreLogger()) CAT_CORE_CRITICAL("AssetManager shutdown must run on its owner thread");
            std::abort();
        }
        m_Impl->closed = true;
        for (auto& pending : m_Impl->pending) pending.job.Cancel();
        for (auto& pending : m_Impl->pending) {
            try { std::string error; m_Impl->jobs.Wait(pending.job, error); } catch (...) {}
            // Error diagnostics may fail, but releasing a still-running owner's budget is never safe.
            if (!m_Impl->jobs.IsComplete(pending.job)) {
                if (Log::GetCoreLogger()) CAT_CORE_CRITICAL("AssetManager could not join its worker before shutdown");
                std::abort();
            }
        }
        m_Impl->pending.clear(); m_Impl->entries.clear(); m_Impl->loaders.clear(); m_Impl->knownDigests.clear();
        m_Impl->jobs.CollectCompleted();
    }
    AssetStatus AssetManager::GetStatus(AssetId id) const { const auto it = m_Impl->entries.find(id); return it == m_Impl->entries.end() ? AssetStatus::Unloaded : it->second.status; }
    std::string AssetManager::GetDiagnostic(AssetId id) const { const auto it = m_Impl->entries.find(id); return it == m_Impl->entries.end() ? "Asset is not loaded" : it->second.diagnostic; }
    MemoryBudgetStats AssetManager::GetBudgetStats() const { return m_Impl->budget.GetStats(); }
    size_t AssetManager::GetPendingCount() const { return m_Impl->pending.size(); }
    bool AssetManager::GetAsset(AssetId id, std::type_index type, AssetSnapshot& output, std::string& error) const {
        if (!m_Impl->Owner(error)) return false;
        const auto* loader = m_Impl->Loader(type);
        if (!id.HasValue() || !loader) { error = "Asset handle or runtime payload type is invalid"; return false; }
        if (const auto* record = m_Impl->registry.Find(id); record && record->type != loader->type) { error = "Typed asset handle does not match its registry type"; return false; }
        const auto found = m_Impl->entries.find(id);
        if (found != m_Impl->entries.end()) {
            const auto& entry = found->second;
            if (entry.type.HasValue() && entry.type != loader->type) { error = "Typed asset handle does not match its registered type"; return false; }
            if (entry.current.asset.value) { output = entry.current; error.clear(); return true; }
        }
        if (!loader->fallback.value) { error = "Asset has no ready version or typed fallback"; return false; }
        output = {loader->fallbackId, loader->fallbackVersion, loader->fallback, true}; error.clear(); return true;
    }
    bool AssetManager::PublishBuiltin(AssetId id, uint64_t version, AssetLoadResult value, std::string& error) {
        try {
            if (!m_Impl->Owner(error)) return false;
            const auto* loader = m_Impl->Loader(value.cppType);
            if (!id.HasValue() || version == 0 || !value.value || value.residentBytes == 0 || !loader) { error = "Invalid builtin identity/version/payload or unregistered type"; return false; }
            if (m_Impl->registry.Find(id)) { error = "Generated builtin identity collides with a cooked registry record"; return false; }
            auto found = m_Impl->entries.find(id);
            if (found != m_Impl->entries.end() && (!found->second.builtin || found->second.type != loader->type || version <= found->second.current.version || found->second.generation == std::numeric_limits<uint64_t>::max())) {
                error = "Builtin must preserve type and monotonically advance its version"; return false;
            }
            MemoryReservation reservation;
            if (!m_Impl->budget.TryReserve(value.residentBytes, reservation)) { error = "Builtin exceeds memory budget"; return false; }
            auto retained = m_Impl->Retain(std::move(value), std::move(reservation));
            auto& entry = m_Impl->entries[id]; entry.job.Cancel(); ++entry.generation; entry.type = loader->type; entry.builtin = true;
            entry.current = {id, version, std::move(retained), false}; entry.status = AssetStatus::Ready; entry.diagnostic.clear(); entry.contentHash.clear();
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
