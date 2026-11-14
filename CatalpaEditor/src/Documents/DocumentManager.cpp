#include "Documents/DocumentManager.h"

#include "Core/PlatformServices.h"

namespace Catalpa::Editor {
	namespace {
		struct AuthorFileIdentity {
			std::filesystem::path path;
			bool exists = false;
		};

		AuthorFileIdentity IdentifyAuthorFile(const std::filesystem::path& path) {
			if (!path.is_absolute())
				throw std::invalid_argument("Author path must be absolute");
			const auto resolved = std::filesystem::weakly_canonical(path);
			return {resolved, std::filesystem::exists(resolved)};
		}

		bool SameAuthorFile(const AuthorFileIdentity& file, const std::filesystem::path& other) {
			const auto occupied = IdentifyAuthorFile(other);
			/*
			 * 相互包含只可能是同一路径；平台比较同时处理 Windows 的文件名大小写。
			 * 现有文件另查文件身份，使硬链接别名也复用同一作者会话。
			 */
			return (IsPathWithinRoot(file.path, occupied.path) && IsPathWithinRoot(occupied.path, file.path)) ||
			    (file.exists && occupied.exists && std::filesystem::equivalent(file.path, occupied.path));
		}
	}

	AssetDocument* DocumentManager::OpenSource(
	    AssetId asset, std::string type, std::string source, const std::filesystem::path& path, std::string& error) {
		if (auto* existing = FindByPath(path, error)) {
			auto* document = dynamic_cast<AssetDocument*>(existing);
			if (!document || document->TypeKey() != type) {
				error = "Author file is already open with another document type";
				return nullptr;
			}
			m_Active = document->Id();
			return document;
		}
		if (!error.empty())
			return nullptr;
		StableId id;
		if (m_Open.size() >= 128 || !GenerateStableId(id, error)) {
			if (error.empty())
				error = "Too many open documents";
			return nullptr;
		}
		auto document = std::make_unique<AssetDocument>(id, asset, std::move(type), std::move(source));
		document->SetAuthorPath(path);
		auto* result = document.get();
		m_Open.emplace(id, std::move(document));
		m_Active = id;
		return result;
	}

	std::vector<Document*> DocumentManager::All() const {
		std::vector<Document*> result;
		for (const auto& [id, document] : m_Open)
			result.push_back(document.get());
		return result;
	}

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

	template <class T> AssetDocument* DocumentManager::OpenAsset(AssetVersionRef<T> source, std::string& error) {
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

	Document* DocumentManager::FindByPath(const std::filesystem::path& path, std::string& error) const try {
		error.clear();
		if (path.empty())
			return nullptr; /* 未保存文档没有文件身份，不能相互合并。 */
		const auto identity = IdentifyAuthorFile(path);
		for (const auto& [id, document] : m_Open)
			if (!document->AuthorPath().empty() && SameAuthorFile(identity, document->AuthorPath()))
				return document.get();
		return nullptr;
	} catch (const std::exception& exception) {
		error = exception.what();
		return nullptr;
	}

	bool DocumentManager::HasDirtyDocuments() const {
		return std::any_of(
		    m_Open.begin(), m_Open.end(), [](const auto& item) { return item.second->History().Dirty(); });
	}

	bool DocumentManager::CanSaveTo(
	    const Document& document, const std::filesystem::path& target, std::string& error) const try {
		const auto identity = IdentifyAuthorFile(target);
		for (const auto& [id, other] : m_Open) {
			if (other.get() == &document || other->AuthorPath().empty())
				continue;
			if (SameAuthorFile(identity, other->AuthorPath())) {
				error = "Save target belongs to another open document; activate or close that document first";
				return false;
			}
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
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
