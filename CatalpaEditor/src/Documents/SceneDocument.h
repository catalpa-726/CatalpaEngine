#pragma once
#include "Documents/Document.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"

namespace Catalpa { class AssetManager; }

namespace Catalpa::Editor {
    // 只持有 SceneHandle，解析时由同一 SceneManager 检查所属实例与代次。
    class SceneDocument final : public Document {
      public:
        SceneDocument(StableId id, SceneManager& scenes, SceneHandle handle)
            : Document(id), m_Scenes(scenes), m_Handle(handle) {}

        Scene* Resolve() const noexcept;
        bool Serialize(std::string&, std::string& error) const override;
        bool StartPlay(AssetManager&, std::string& error);
        bool StopPlay(std::string& error);
        bool AdoptPlayScene(SceneHandle candidate, std::string& error);
        bool Playing() const noexcept { return m_Play.HasValue(); }
        SceneHandle PlayHandle() const noexcept { return m_Play; }
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
        SceneHandle m_Play;
        std::map<PersistentEntityId, EntityId> m_Targets;
    };
}
