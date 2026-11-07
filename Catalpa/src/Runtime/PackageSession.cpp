#include "catpch.h"
#include "Runtime/PackageSession.h"
#include "Assets/AssetManager.h"
#include "Scene/SceneSerializer.h"

namespace Catalpa {
    bool PackageSession::Start(std::shared_ptr<const PackageReader> package, AssetManager& assets,
                               SceneManager& scenes, std::string& error) {
        if (m_Package || !package) {
            error = "Package session is already active or package is null";
            return false;
        }
        if (!assets.InstallPackage(package, error))
            return false;
        m_Package = std::move(package);
        m_Previous = scenes.GetActiveHandle();
        m_State = PackageSessionState::Loading;
        if (!assets.Request(m_Package->EntryScene(), error)) {
            m_State = PackageSessionState::Stopping;
            std::string ignored;
            Stop(assets, scenes, ignored);
            return false;
        }
        error.clear();
        return true;
    }

    bool PackageSession::Pump(AssetManager& assets, SceneManager& scenes, std::string& error) {
        if (m_State == PackageSessionState::Stopping) {
            if (assets.GetPendingCount()) {
                error.clear();
                return true;
            }
            return Stop(assets, scenes, error);
        }
        if (m_State != PackageSessionState::Loading) {
            error.clear();
            return true;
        }
        const auto failAndRetire = [&] {
            const auto diagnostic = error;
            m_State = PackageSessionState::Stopping;
            std::string retirement;
            const bool stopped = Stop(assets, scenes, retirement);
            error = diagnostic;
            if (!stopped && !retirement.empty())
                error += "; retirement pending: " + retirement;
            return false;
        };
        const auto status = assets.GetStatus(m_Package->EntryScene());
        if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
            error = assets.GetDiagnostic(m_Package->EntryScene());
            return failAndRetire();
        }
        if (status != AssetStatus::Ready) {
            error.clear();
            return true;
        }
        AssetVersionRef<SceneAsset> asset;
        bool fallback = false;
        if (!assets.Acquire(m_Package->EntryScene(), asset, fallback, error) || fallback ||
            !SceneSerializer::Instantiate(*asset.Get(), scenes, &assets, m_Scene, error)) {
            if (error.empty())
                error = "Package entry scene cannot use a fallback";
            return failAndRetire();
        }
        const auto activated = scenes.SetActive(m_Scene);
        if (!activated.IsSuccess()) {
            error = activated.message;
            return failAndRetire();
        }
        m_State = PackageSessionState::Running;
        error.clear();
        return true;
    }

    bool PackageSession::Stop(AssetManager& assets, SceneManager& scenes, std::string& error) {
        if (!m_Package) {
            m_State = PackageSessionState::Idle;
            error.clear();
            return true;
        }
        m_State = PackageSessionState::Stopping;
        if (m_Scene.HasValue()) {
            if ((scenes.GetActiveHandle() == m_Scene || !scenes.GetActiveHandle().HasValue()) &&
                m_Previous.HasValue() && scenes.GetScene(m_Previous)) {
                const auto restored = scenes.SetActive(m_Previous);
                if (!restored.IsSuccess()) {
                    error = restored.message;
                    return false;
                }
            }
            const auto unloaded = scenes.Unload(m_Scene);
            if (!unloaded.IsSuccess()) {
                error = unloaded.message;
                return false;
            }
            m_Scene = {};
        }
        for (const auto& record : m_Package->Catalog().GetRecords())
            assets.Cancel(record.id);
        assets.Pump();
        if (!assets.UninstallPackage(error))
            return false;
        ResetAfterShutdown();
        error.clear();
        return true;
    }

    void PackageSession::ResetAfterShutdown() noexcept {
        m_Package.reset();
        m_Scene = {};
        m_Previous = {};
        m_State = PackageSessionState::Idle;
    }
} // namespace Catalpa
