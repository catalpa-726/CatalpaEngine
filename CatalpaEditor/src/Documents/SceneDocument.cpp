#include "Documents/SceneDocument.h"
#include "Scene/SceneSubtree.h"

namespace Catalpa::Editor {
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
