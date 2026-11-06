#pragma once
#include "Documents/AssetDocument.h"
#include "Documents/SceneDocument.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa::Editor {
    // 同一场景或固定资产版本只打开一次；文档管理器不拥有 Scene/World。
    class DocumentManager {
      public:
        SceneDocument* SceneFor(SceneManager&, SceneHandle, std::string& error);
        AssetDocument* Open(AssetVersionRef<MaterialInstance>, std::string& error);
        AssetDocument* Open(AssetVersionRef<ImageAsset>, std::string& error);
        AssetDocument* OpenSource(AssetId, std::string type, std::string source,
                                  const std::filesystem::path& path, std::string& error);
        std::vector<Document*> All() const;
        Document* Active() const noexcept;
        Document* Find(StableId id) const noexcept;
        Document* FindByPath(const std::filesystem::path&, std::string& error) const;
        bool HasDirtyDocuments() const;
        bool CanSaveTo(const Document&, const std::filesystem::path& target, std::string& error) const;
        std::vector<AssetDocument*> Assets() const;
        void Activate(StableId);
        void Close(StableId);
        void CloseStaleScenes();
        void Clear();

      private:
        template <class T> AssetDocument* OpenAsset(AssetVersionRef<T>, std::string&);
        std::map<StableId, std::unique_ptr<Document>> m_Open;
        StableId m_Active;
    };
}
