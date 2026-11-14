#include "AssetEditors/TerrainEditor.h"
#include "AssetEditors/AnimationEditor.h"
#include "AssetEditors/UIDocumentEditor.h"
#include "AssetEditors/ParticleEditor.h"
#include "AssetEditors/PhysicsNavigationEditor.h"
#include "AssetEditors/AssetEditorRegistry.h"

#include "AssetEditors/MaterialEditor.h"
#include "AssetEditors/TexturePreviewEditor.h"
#include "AssetEditors/MaterialGraphEditor.h"
#include "AssetEditors/BehaviorGraphEditor.h"
#include "AssetEditors/ScriptEditor.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		template <class T> std::unique_ptr<IAssetEditor> CreateEditor() {
			return std::make_unique<T>();
		}

		template <class T>
		AssetDocument* OpenVersion(AssetId id, const AssetManager& assets, DocumentManager& docs, std::string& error) {
			AssetVersionRef<T> fixed;
			bool fallback = false;
			if (!assets.Acquire(id, fixed, fallback, error))
				return nullptr;
			if (fallback) {
				error = "An asset draft requires the selected version rather than its fallback";
				return nullptr;
			}
			return docs.Open(std::move(fixed), error);
		}

		AssetDocument* AuthorOnly(AssetId, const AssetManager&, DocumentManager&, std::string& error) {
			error = "Open the source document from Documents or its successful author build";
			return nullptr;
		}
	}

	AssetEditorRegistry::AssetEditorRegistry() {
		std::string error;
		if (!RegisterAnimationEditors(*this, error) || !RegisterAudioUIEditors(*this, error) ||
		    !RegisterParticleEditor(*this, error))
			throw std::logic_error(error);
		if (!Register("terrain", &CreateEditor<TerrainEditor>, &AuthorOnly, error))
			throw std::logic_error(error);
		for (auto type : {"collision_shape", "physics_material", "navmesh"})
			if (!Register(type, &CreateEditor<PhysicsNavigationEditor>, &AuthorOnly, error))
				throw std::logic_error(error);
		if (!Register("material_instance", &CreateEditor<MaterialEditor>, &OpenVersion<MaterialInstance>, error) ||
		    !Register("image", &CreateEditor<TexturePreviewEditor>, &OpenVersion<ImageAsset>, error) ||
		    !Register("script", &CreateEditor<ScriptEditor>, &AuthorOnly, error) ||
		    !Register("prefab", &CreateEditor<ScriptEditor>, &AuthorOnly, error) ||
		    !Register("material_definition", &CreateEditor<ScriptEditor>, &AuthorOnly, error) ||
		    !Register("material_graph", &CreateEditor<MaterialGraphEditor>, &AuthorOnly, error) ||
		    !Register("behavior_graph", &CreateEditor<BehaviorGraphEditor>, &AuthorOnly, error))
			throw std::logic_error(error);
	}

	bool AssetEditorRegistry::Register(
	    std::string_view type, Factory factory, OpenDocument open, std::string& error) try {
		AssetType key;
		if (!factory || !open || !AssetType::TryParse(type, key)) {
			error = "Invalid asset editor registration";
			return false;
		}
		if (!m_Entries.try_emplace(key.ToString(), Entry{factory, open}).second) {
			error = "Duplicate asset editor type";
			return false;
		}
		error.clear();
		return true;
	} catch (const std::bad_alloc&) {

		error = "Cannot reserve asset editor registration";
		return false;
	}

	AssetDocument* AssetEditorRegistry::Open(AssetId id, std::string_view type, const AssetManager& assets,
	    DocumentManager& docs, std::string& error) const try {
		const auto found = m_Entries.find(type);
		if (found == m_Entries.end()) {
			error = "No editor registered for asset type: " + std::string(type);
			return nullptr;
		}
		return found->second.open(id, assets, docs, error);
	} catch (const std::exception& failure) {

		error = failure.what();
		return nullptr;
	}

	void AssetEditorRegistry::Close(Application& app) {
		if (m_Active)
			m_Active->Close(app);
		m_Active.reset();
		m_ActiveType = {};
	}

	void AssetEditorRegistry::Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) {
		const auto key = doc.TypeKey();
		if (!m_Active || m_ActiveType != key) {
			const auto found = m_Entries.find(key);
			if (found == m_Entries.end()) {
				error = "Document type has no preview editor";
				return;
			}
			std::unique_ptr<IAssetEditor> editor;
			try {
				editor = found->second.factory();
				if (!editor || !editor->Accepts(doc)) {
					error = "Editor factory cannot display this document type";
					return;
				}
			} catch (const std::exception& failure) {
				error = failure.what();
				return;
			}
			Close(app);
			m_Active = std::move(editor);
			m_ActiveType = found->first;
		}
		ImGui::PushID(doc.Id().ToString().c_str());
		struct Scope {
			~Scope() { ImGui::PopID(); }
		} scope;
		m_Active->Draw(app, doc, text, error);
	}
}
