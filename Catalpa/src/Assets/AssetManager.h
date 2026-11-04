#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Memory/MemoryBudget.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Assets/AssetRegistry.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetStatus.h"
namespace Catalpa {
    struct AssetManagerConfig {
        // Payload capacities and bounded decode workspace, excluding allocator/control-block overhead.
        // This is managed CPU asset accounting, not a measurement of total process or GPU memory.
        size_t maxFileBytes = 16 * 1024 * 1024;
        size_t maxResidentBytes = 32 * 1024 * 1024;
        size_t memoryBudgetBytes = 512 * 1024 * 1024;
        size_t maxPending = 32;
        // Bounded identity history prevents reusing a published (ID, content version) with other bytes.
        size_t maxKnownVersions = 4096;
    };
    struct AssetSnapshot {
        AssetId id;
        uint64_t version = 0;
        AssetLoadResult asset;
        bool fallback = false;
    };
    // Main-thread owner. Workers receive value snapshots, never this manager or mutable records.
    // CPU snapshots keep their precise version and budget reservation after reload/unload/shutdown.
    // Shutdown joins outstanding callbacks; wrong-thread destruction is a fatal lifecycle violation.
    class CATALPA_API AssetManager {
    public:
        AssetManager(JobSystem&, VirtualFileSystem files = {}, AssetRegistry registry = {}, AssetManagerConfig config = {});
        ~AssetManager();
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        bool RegisterLoader(AssetLoader, std::string& error);
        bool SetFallback(AssetId, std::type_index, std::string& error);
        template<class T> bool SetFallback(AssetId id, std::string& error) { return SetFallback(id, std::type_index(typeid(T)), error); }
        bool ReplaceRegistry(AssetRegistry, std::string& error);
        // Cooked versions are opaque nonzero content keys; request generations provide ordering.
        bool Request(AssetId, std::string& error, bool forceReload = false);
        void Cancel(AssetId);
        void Unload(AssetId);
        void Pump();
        void Shutdown() noexcept;
        [[nodiscard]] AssetStatus GetStatus(AssetId) const;
        [[nodiscard]] std::string GetDiagnostic(AssetId) const;
        [[nodiscard]] MemoryBudgetStats GetBudgetStats() const;
        [[nodiscard]] size_t GetPendingCount() const;
        bool GetAsset(AssetId, std::type_index, AssetSnapshot&, std::string& error) const;
        // Explicit engine-generated values use the same identity/version/cache path and memory budget.
        // Builtin producers supply increasing revisions; they cannot replace cooked registry identities.
        bool PublishBuiltin(AssetId, uint64_t version, AssetLoadResult, std::string& error);
        template<class T> bool PublishBuiltin(AssetId id, uint64_t version, std::shared_ptr<const T> value,
            size_t bytes, std::string& error) {
            return PublishBuiltin(id, version, MakeAssetLoadResult<T>(std::move(value), bytes), error);
        }
        template<class T> bool Acquire(AssetId id, AssetVersionRef<T>& output, bool& usedFallback, std::string& error) const {
            AssetSnapshot snapshot;
            if (!GetAsset(id, std::type_index(typeid(T)), snapshot, error)) return false;
            if (!AssetVersionRef<T>::TryCreate(snapshot.id, snapshot.version,
                std::static_pointer_cast<const T>(snapshot.asset.value), output, error)) return false;
            usedFallback = snapshot.fallback; return true;
        }
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
