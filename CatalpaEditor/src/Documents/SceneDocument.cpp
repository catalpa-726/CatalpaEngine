#include "Documents/SceneDocument.h"
#include "Scene/SceneSubtree.h"
#include "Scene/SceneSerializer.h"

namespace Catalpa::Editor {
    bool SceneDocument::StartPlay(AssetManager& assets, std::string& error) try {
        if (Playing()) { error = "Document already has a Play session"; return false; }
        auto* author = Resolve();
        SceneAsset snapshot;
        SceneHandle candidate;
        if (!author || !SceneSerializer::Capture(*author, snapshot, error) ||
            !SceneSerializer::Instantiate(snapshot, m_Scenes, &assets, candidate, error)) return false;
        struct Candidate {
            SceneManager& scenes;
            SceneHandle handle;
            bool committed = false;
            ~Candidate() { if (!committed) (void)scenes.Unload(handle); }
        } guard{m_Scenes, candidate};
        auto status = author->GetSystems().CloneGameplayTo(*m_Scenes.GetScene(candidate));
        if (status.IsSuccess()) status = m_Scenes.SetActive(candidate);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        m_Play = candidate;
        guard.committed = true;
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool SceneDocument::StopPlay(std::string& error) {
        if (!Playing()) { error.clear(); return true; }
        auto status = m_Scenes.SetActive(m_Handle);
        if (status.IsSuccess() && m_Scenes.GetScene(m_Play)) status = m_Scenes.Unload(m_Play);
        if (!status.IsSuccess()) { error = status.message; return false; }
        m_Play = {};
        error.clear();
        return true;
    }
    bool SceneDocument::Serialize(std::string& text, std::string& error) const {
        const auto* scene = Resolve();
        SceneAsset asset;
        if (!scene) { error = "Scene was unloaded"; return false; }
        return SceneSerializer::Capture(*scene, asset, error) && SceneSerializer::Write(asset, text, error);
    }
    Scene* SceneDocument::Resolve() const noexcept {
        return m_Scenes.GetScene(m_Handle);
    }

    bool SceneDocument::CheckTarget(PersistentEntityId id, std::string& error) {
        if (!id.HasValue())
            return true;
        const auto* scene = Resolve();
        const auto entity = scene ? scene->Resolve(id) : EntityId{};
        if (!entity.HasValue()) {
            error = "Command target is missing";
            return false;
        }
        const auto [it, added] = m_Targets.try_emplace(id, entity);
        if (!added && it->second != entity) {
            error = "Command target was replaced outside this document";
            return false;
        }
        return true;
    }

    void SceneDocument::ReserveIdentity(PersistentEntityId id) {
        m_Targets.try_emplace(id, EntityId{});
    }

    void SceneDocument::TrackCreated(PersistentEntityId id) noexcept {
        const auto it = m_Targets.find(id);
        if (it != m_Targets.end() && Resolve())
            it->second = Resolve()->Resolve(id);
    }

    void SceneDocument::RefreshDeletedTargets() noexcept {
        if (auto* scene = Resolve())
            for (auto& [id, entity] : m_Targets)
                if (entity.HasValue() && !scene->GetWorld().IsAlive(entity))
                    entity = {};
    }

    void SceneDocument::RefreshRestoredTargets(const SceneSubtreeSnapshot& snapshot) noexcept {
        snapshot.ForEachIdentity([&](PersistentEntityId id) { TrackCreated(id); });
    }
}

namespace Catalpa::Editor {
    bool SceneDocument::AdoptPlayScene(SceneHandle candidate, std::string& error) {
        if (!Playing() || candidate == m_Handle || candidate == m_Play || !m_Scenes.GetScene(candidate)) {
            error = "Invalid replacement Play scene";
            return false;
        }
        const auto previous = m_Play;
        auto status = m_Scenes.SetActive(candidate);
        if (status.IsSuccess())
            status = m_Scenes.Unload(previous);
        if (!status.IsSuccess()) {
            (void)m_Scenes.SetActive(previous);
            error = status.message;
            return false;
        }
        m_Play = candidate;
        error.clear();
        return true;
    }
} // namespace Catalpa::Editor
