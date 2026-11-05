#include "Documents/DocumentManager.h"

namespace Catalpa::Editor {
    SceneDocument* DocumentManager::SceneFor(SceneManager& scenes, SceneHandle scene, std::string& error) {
        if (!scenes.GetScene(scene)) {
            error = "Scene was unloaded";
            return nullptr;
        }
        for (auto& [id, doc] : m_Open) {
            auto* found = dynamic_cast<SceneDocument*>(doc.get());
            if (found && found->Handle() == scene)
                return found;
        }
        if (m_Open.size() >= 128) {
            error = "Close a document before opening another";
            return nullptr;
        }
        StableId id;
        if (!GenerateStableId(id, error) || m_Open.contains(id))
            return nullptr;
        auto doc = std::make_unique<SceneDocument>(id, scenes, scene);
        auto* result = doc.get();
        m_Open.emplace(id, std::move(doc));
        if (!m_Active.HasValue())
            m_Active = id;
        return result;
    }

    template <class T>
    AssetDocument* DocumentManager::OpenAsset(AssetVersionRef<T> source, std::string& error) {
        if (!source.HasValue()) {
            error = "Asset version is unavailable";
            return nullptr;
        }
        for (auto& [id, doc] : m_Open) {
            auto* found = dynamic_cast<AssetDocument*>(doc.get());
            if (found && found->Asset() == source.GetId() && found->SourceVersion() == source.GetVersion()) {
                m_Active = id;
                return found;
            }
        }
        if (m_Open.size() >= 128) {
            error = "Close a document before opening another";
            return nullptr;
        }
        StableId id;
        if (!GenerateStableId(id, error) || m_Open.contains(id))
            return nullptr;
        auto doc = std::make_unique<AssetDocument>(id, std::move(source));
        auto* result = doc.get();
        m_Open.emplace(id, std::move(doc));
        m_Active = id;
        return result;
    }

    AssetDocument* DocumentManager::Open(AssetVersionRef<MaterialInstance> source, std::string& e) {
        return OpenAsset(source, e);
    }

    AssetDocument* DocumentManager::Open(AssetVersionRef<ImageAsset> source, std::string& e) {
        return OpenAsset(source, e);
    }

    Document* DocumentManager::Active() const noexcept {
        return Find(m_Active);
    }

    Document* DocumentManager::Find(StableId id) const noexcept {
        const auto it = m_Open.find(id);
        return it == m_Open.end() ? nullptr : it->second.get();
    }

    bool DocumentManager::HasDirtyDocuments() const {
        return std::any_of(m_Open.begin(), m_Open.end(),
                           [](const auto& item) { return item.second->History().Dirty(); });
    }

    std::vector<AssetDocument*> DocumentManager::Assets() const {
        std::vector<AssetDocument*> assets;
        for (const auto& [id, doc] : m_Open)
            if (auto* asset = dynamic_cast<AssetDocument*>(doc.get()))
                assets.push_back(asset);
        return assets;
    }

    void DocumentManager::Activate(StableId id) {
        if (m_Open.contains(id))
            m_Active = id;
    }

    void DocumentManager::Close(StableId id) {
        m_Open.erase(id);
        if (m_Active == id)
            m_Active = {};
    }

    void DocumentManager::CloseStaleScenes() {
        std::erase_if(m_Open, [&](const auto& entry) {
            auto* scene = dynamic_cast<SceneDocument*>(entry.second.get());
            if (!scene || scene->Resolve())
                return false;
            if (m_Active == entry.first)
                m_Active = {};
            return true;
        });
    }

    void DocumentManager::Clear() {
        m_Open.clear();
        m_Active = {};
    }
}
