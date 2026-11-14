#include "AssetEditors/FontPreviewEditor.h"

#include "AssetEditors/AssetEditorRegistry.h"
#include "Core/Application.h"
#include "Core/PlatformServices.h"
#include "Core/Id/StableIdGenerator.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		constexpr uint64_t FontSurface = 0x434154464f4e5450ULL;

		std::unique_ptr<IAssetEditor> CreateFontEditor() {
			return std::make_unique<FontPreviewEditor>();
		}

		AssetDocument* OpenFont(AssetId id, const AssetManager& assets, DocumentManager& docs, std::string& error) {
			AssetVersionRef<FontAsset> font;
			bool fallback = false;
			if (!assets.Acquire(id, font, fallback, error) || fallback)
				return nullptr;
			return docs.OpenSource(id, "font", "{}", {}, error);
		}

		std::string EscapeFontText(std::string_view text) {
			std::string output;
			for (char c : text)
				switch (c) {
				case '&':
					output += "&amp;";
					break;
				case '<':
					output += "&lt;";
					break;
				case '>':
					output += "&gt;";
					break;
				case '\n':
					output += "<br/>";
					break;
				default:
					output += c;
				}
			return output;
		}
	} /* 命名空间 */
	bool RegisterFontPreviewEditor(AssetEditorRegistry& registry, std::string& error) {
		return registry.Register("font", &CreateFontEditor, &OpenFont, error);
	}

	void FontPreviewEditor::Close(Application& app) {
		app.CloseEditorPreview(FontSurface);
		m_Runtime.reset();
		if (app.GetScenes().GetScene(m_Scene))
			app.GetScenes().Unload(m_Scene);
		m_Scene = {};
		m_Document = {};
		m_Elapsed = 0;
		m_FontVersion = 0;
	}

	void FontPreviewEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) {
		AssetVersionRef<FontAsset> font;
		bool fallback = false;
		if (!app.GetAssets().Acquire(doc.Asset(), font, fallback, error))
			return;
		ImGui::Text("%s | %u %s", font.Get()->family.c_str(), font.Get()->glyphCount,
		    text("font.glyphs", "glyphs", nullptr).c_str());
		ImGui::TextWrapped("%s", font.Get()->sourceUri.c_str());
		ImGui::TextUnformatted(
		    text("font.read_only", "Font source bytes stay read-only; preview uses the exact cooked version.", nullptr)
		        .c_str());
		if (m_Document != doc.Id() || m_FontVersion != font.GetVersion()) {
			Close(app);
			std::filesystem::path root;
			if (!GetExecutableDirectory(root, error))
				return;
			std::ifstream file(root / "Resources/Config/FontProfiles.json", std::ios::binary | std::ios::ate);
			if (!file) {
				error = "Cannot open deployed Resources/Config/FontProfiles.json";
				return;
			}
			const auto length = file.tellg();
			if (length < 0 || length > 65536) {
				error = "FontProfiles byte limit rejected";
				return;
			}
			std::string profile(size_t(length), '\0');
			file.seekg(0);
			if (!profile.empty() && !file.read(profile.data(), std::streamsize(profile.size()))) {
				error = "Cannot read FontProfiles";
				return;
			}
			if (!ReadFontProfile(profile, m_Profile, error))
				return;
			UIDocumentAsset preview;
			preview.entry = "Preview.rml";
			preview.fontIds["font.ttf"] = font.GetId();
			preview.fonts["font.ttf"] = font;
			preview.files[preview.entry] = "<rml><head><style>body { margin: 12px; color: white; font-size: " +
			    std::to_string(m_Profile.previewPixelSize) + "px; }</style></head><body>" +
			    EscapeFontText(m_Profile.previewText) + "</body></rml>";
			StableId id;
			if (!GenerateStableId(id, error))
				return;
			AssetVersionRef<UIDocumentAsset> fixed;
			if (!AssetVersionRef<UIDocumentAsset>::TryCreate(
			        AssetId{id}, 1, std::make_shared<const UIDocumentAsset>(std::move(preview)), fixed, error))
				return;
			auto runtime = std::make_unique<UIRuntime>();
			if (!runtime->Initialize(fixed, m_Profile.previewWidth, m_Profile.previewHeight, error))
				return;
			auto status = app.GetScenes().CreateScene("Font preview", m_Scene);
			if (!status.IsSuccess()) {
				error = status.message;
				return;
			}
			auto* scene = app.GetScenes().GetScene(m_Scene);
			PersistentEntityId camera;
			if (!scene || !GeneratePersistentEntityId(camera, error))
				return;
			status = scene->QueueCreate(camera, "Font preview camera", {});
			if (status.IsSuccess())
				status = scene->CommitPending();
			if (status.IsSuccess())
				status = scene->QueueSetCamera(scene->Resolve(camera), {});
			if (status.IsSuccess())
				status = scene->CommitPending();
			if (!status.IsSuccess()) {
				error = status.message;
				return;
			}
			m_Runtime = std::move(runtime);
			m_Document = doc.Id();
			m_FontVersion = font.GetVersion();
		}
		if (!m_Runtime)
			return;
		m_Elapsed += std::clamp(double(ImGui::GetIO().DeltaTime), 0.0, 1.0);
		std::shared_ptr<const UIDrawData> draw;
		if (!m_Runtime->Advance(m_Elapsed, error) || !m_Runtime->Render(draw, error) ||
		    !app.SetEditorPreview(m_Scene, FontSurface, {m_Profile.previewWidth, m_Profile.previewHeight}, error) ||
		    !app.SetEditorGameUI(FontSurface, std::move(draw), error))
			return;
		RenderSurfaceSnapshot surface;
		if (app.GetEditorPreview(FontSurface, surface) && surface.textureToken) {
			const auto width = std::min(ImGui::GetContentRegionAvail().x, float(m_Profile.previewWidth));
			ImGui::Image(ImTextureRef(static_cast<ImTextureID>(surface.textureToken)),
			    {width, width * m_Profile.previewHeight / m_Profile.previewWidth});
		}
	}
} /* 命名空间 Catalpa::Editor */
