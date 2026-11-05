#pragma once
#include "Documents/Document.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"

namespace Catalpa::Editor {
    // 只持有 SceneHandle，解析时由同一 SceneManager 检查所属实例与代次。
    class SceneDocument final : public Document {
      public:
        SceneDocument(StableId id, SceneManager& scenes, SceneHandle handle)
            : Document(id), m_Scenes(scenes), m_Handle(handle) {}

        Scene* Resolve() const noexcept;
        // 首次使用时记录代次，只有本文件的受控删除/恢复能重新绑定持久身份。
        bool CheckTarget(PersistentEntityId, std::string& error);
        void RefreshDeletedTargets() noexcept;
        void RefreshRestoredTargets(const SceneSubtreeSnapshot&) noexcept;
        void TrackCreated(PersistentEntityId id) noexcept;
        void ReserveIdentity(PersistentEntityId id);

        SceneHandle Handle() const noexcept {
            return m_Handle;
        }

        SceneManager& Scenes() const noexcept {
            return m_Scenes;
        }

      private:
        SceneManager& m_Scenes;
        SceneHandle m_Handle;
        std::map<PersistentEntityId, EntityId> m_Targets;
    };
}
