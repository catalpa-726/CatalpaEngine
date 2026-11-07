#include "AssetEditors/UIDocumentEditor.h"
#include "AssetEditors/AudioPreviewEditor.h"
#include "AssetEditors/FontPreviewEditor.h"
#include "AssetEditors/AssetEditorRegistry.h"
#include "Commands/GraphCommands.h"
#include "Core/Application.h"
#include "Core/KeyCodes.h"
#include "UI/UIInputRouter.h"
#include "Log/Log.h"
#include "Core/Serialization/AuthorJson.h"
#include "Core/Id/StableIdGenerator.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t UISurface = 0x4341545549444f43ULL;
        using Json = AuthorJson::Json;
        template <class T> std::unique_ptr<IAssetEditor> Factory() {
            return std::make_unique<T>();
        }
        AssetDocument* OpenAudio(AssetId id, const AssetManager& assets, DocumentManager& docs,
                                 std::string& error) {
            AssetVersionRef<AudioClipAsset> clip;
            bool fallback = false;
            if (!assets.Acquire(id, clip, fallback, error) || fallback)
                return nullptr;
            const Json value{{"schemaVersion", 1}, {"source", clip.Get()->sourceUri}};
            return docs.OpenSource(id, "audio_clip", value.dump(2), {}, error);
        }
        AssetDocument* OpenUI(AssetId id, const AssetManager& assets, DocumentManager& docs,
                              std::string& error) {
            AssetVersionRef<UIDocumentAsset> ui;
            bool fallback = false;
            if (!assets.Acquire(id, ui, fallback, error) || fallback)
                return nullptr;
            const auto& value = *ui.Get();
            Json images = Json::object(), fonts = Json::object();
            for (const auto& [path, asset] : value.imageIds)
                images[path] = asset.ToString();
            for (const auto& [path, asset] : value.fontIds)
                fonts[path] = asset.ToString();
            const Json source{{"schemaVersion", 1},     {"document", value.entry},
                              {"files", Json::array()}, {"inlineFiles", value.files},
                              {"images", images},       {"fonts", fonts},
                              {"values", value.values}};
            return docs.OpenSource(id, "ui_document", source.dump(2), {}, error);
        }
        bool Draft(const AssetDocument& doc, const AssetManager& assets, UIDocumentAsset& output,
                   std::string& error) {
            try {
                const auto source = AuthorJson::Parse(*doc.Source(), 10 * 1024 * 1024);
                AuthorJson::Fields(source, {"schemaVersion", "document", "files", "inlineFiles", "images",
                                            "fonts", "fontSources", "values"});
                if (AuthorJson::Unsigned(source.at("schemaVersion")) != 1)
                    throw std::invalid_argument("Unsupported UI author schema");
                UIDocumentAsset value;
                value.entry = source.at("document").get<std::string>();
                if (source.contains("inlineFiles"))
                    value.files = source.at("inlineFiles").get<decltype(value.files)>();
                // 外置 RML/RCSS 已由 Cook 产物固定；编辑预览不会从任意磁盘路径补读资源。
                if (source.contains("files") && !source.at("files").empty()) {
                    AssetVersionRef<UIDocumentAsset> cooked;
                    bool fallback = false;
                    if (!assets.Acquire(doc.Asset(), cooked, fallback, error) || fallback)
                        return false;
                    for (const auto& path : source.at("files")) {
                        const auto key = path.get<std::string>();
                        const auto found = cooked.Get()->files.find(key);
                        if (found == cooked.Get()->files.end())
                            throw std::invalid_argument("Cook external UI file before preview: " + key);
                        value.files.emplace(key, found->second);
                    }
                }
                if (source.contains("values"))
                    value.values = source.at("values").get<decltype(value.values)>();
                for (const auto* key : {"images", "fonts"})
                    if (source.contains(key)) {
                        if (!source.at(key).is_object())
                            throw std::invalid_argument("UI resource bindings must be an object");
                        for (auto it = source.at(key).begin(); it != source.at(key).end(); ++it) {
                            const auto id = AuthorJson::Identity<AssetId>(it.value());
                            bool fallback = false;
                            if (std::string_view(key) == "images") {
                                AssetVersionRef<ImageAsset> ref;
                                if (!assets.Acquire(id, ref, fallback, error) || fallback)
                                    return false;
                                value.imageIds.emplace(it.key(), id);
                                value.images.emplace(it.key(), std::move(ref));
                            } else {
                                AssetVersionRef<FontAsset> ref;
                                if (!assets.Acquire(id, ref, fallback, error) || fallback)
                                    return false;
                                value.fontIds.emplace(it.key(), id);
                                value.fonts.emplace(it.key(), std::move(ref));
                            }
                        }
                    }
                if (source.contains("fontSources")) {
                    AssetVersionRef<UIDocumentAsset> cooked;
                    bool fallback = false;
                    if (!assets.Acquire(doc.Asset(), cooked, fallback, error) || fallback)
                        return false;
                    for (auto it = source.at("fontSources").begin(); it != source.at("fontSources").end();
                         ++it) {
                        const auto font = cooked.Get()->fonts.find(it.key());
                        if (font == cooked.Get()->fonts.end())
                            throw std::invalid_argument("Cook UI font source before preview: " + it.key());
                        if (!value.fontIds.emplace(it.key(), font->second.GetId()).second)
                            throw std::invalid_argument("Duplicate UI font alias");
                        value.fonts.emplace(it.key(), font->second);
                    }
                }
                if (!ValidateUIDocument(value, error))
                    return false;
                output = std::move(value);
                return true;
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
        }
    } // namespace
    bool RegisterAudioUIEditors(AssetEditorRegistry& registry, std::string& error) {
        return registry.Register("audio_clip", &Factory<AudioPreviewEditor>, &OpenAudio, error) &&
               registry.Register("ui_document", &Factory<UIDocumentEditor>, &OpenUI, error) &&
               RegisterFontPreviewEditor(registry, error);
    }
    void UIDocumentEditor::Close(Application& app) {
        app.GetWindow().ClearTextInputTarget(UISurface);
        app.CloseEditorPreview(UISurface);
        m_Runtime.reset();
        if (app.GetScenes().GetScene(m_Scene))
            app.GetScenes().Unload(m_Scene);
        m_Scene = {};
        m_Document = {};
        m_Bound.reset();
        m_TextSource.reset();
        m_Text.clear();
        m_Elapsed = 0;
        m_PointerCaptured = false;
    }
    bool UIDocumentEditor::BuildPreview(Application& app, AssetDocument& doc, std::string& error) {
        UIDocumentAsset draft;
        if (!Draft(doc, app.GetAssets(), draft, error))
            return false;
        if (m_Version == UINT64_MAX) {
            error = "UI preview version exhausted";
            return false;
        }
        AssetVersionRef<UIDocumentAsset> ref;
        if (!AssetVersionRef<UIDocumentAsset>::TryCreate(
                doc.Asset(), m_Version++, std::make_shared<const UIDocumentAsset>(std::move(draft)), ref,
                error))
            return false;
        auto runtime = std::make_shared<UIRuntime>();
        if (!runtime->Initialize(ref, 640, 360, error))
            return false;
        if (!m_Scene.HasValue()) {
            SceneHandle candidate;
            auto status = app.GetScenes().CreateScene("Game UI preview", candidate);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            struct RollbackScene {
                SceneManager& scenes;
                SceneHandle candidate;
                bool published = false;
                ~RollbackScene() {
                    if (!published)
                        (void)scenes.Unload(candidate);
                }
            } rollback{app.GetScenes(), candidate};
            auto* scene = app.GetScenes().GetScene(candidate);
            PersistentEntityId camera;
            if (!scene || !GeneratePersistentEntityId(camera, error))
                return false;
            status = scene->QueueCreate(camera, "UI preview camera", {});
            if (status.IsSuccess())
                status = scene->CommitPending();
            if (status.IsSuccess())
                status = scene->QueueSetCamera(scene->Resolve(camera), CameraComponent{});
            if (status.IsSuccess())
                status = scene->CommitPending();
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            // 只有相机和场景都成功后才发布句柄；失败预览不能留下无法重试的半成品场景。
            m_Scene = candidate;
            rollback.published = true;
        }
        app.GetWindow().ClearTextInputTarget(UISurface);
        m_Runtime = std::move(runtime);
        m_Bound = doc.Source();
        m_Elapsed = 0;
        m_PointerCaptured = false;
        return true;
    }
    void UIDocumentEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text,
                                std::string& error) {
        if (!doc.Source()) {
            error = "Game UI editor requires an author document";
            return;
        }
        if (m_Document != doc.Id()) {
            Close(app);
            m_Document = doc.Id();
        }
        if (m_Text.empty() || m_TextSource != doc.Source()) {
            if (doc.Source()->size() >= 2 * 1024 * 1024) {
                error = "UI text editor budget exceeds 2 MiB";
                return;
            }
            m_Text.assign(2 * 1024 * 1024, 0);
            std::copy(doc.Source()->begin(), doc.Source()->end(), m_Text.begin());
            m_TextSource = doc.Source();
            if (!BuildPreview(app, doc, error))
                ImGui::TextWrapped("%s", error.c_str());
        }
        ImGui::TextUnformatted(
            text("ui.author", "UI author JSON (RML/RCSS in inlineFiles)", nullptr).c_str());
        ImGui::InputTextMultiline("##UIAuthor", m_Text.data(), m_Text.size(), {0, 180});
        if (ImGui::Button(text("ui.apply", "Apply draft and preview", "UIApply").c_str())) {
            const std::string candidate = m_Text.data();
            try {
                AuthorJson::Parse(candidate, 2 * 1024 * 1024);
            } catch (const std::exception& e) {
                error = e.what();
                return;
            }
            if (!ChangeAuthorSource(doc, candidate, error) || !BuildPreview(app, doc, error))
                return;
        }
        if (!m_Runtime)
            return;
        m_Elapsed += std::clamp(double(ImGui::GetIO().DeltaTime), 0.0, 1.0);
        if (!m_Runtime->Advance(m_Elapsed, error))
            return;
        std::shared_ptr<const UIDrawData> draw;
        if (!m_Runtime->Render(draw, error))
            return;
        if (!app.SetEditorPreview(m_Scene, UISurface, {640, 360}, error) ||
            !app.SetEditorGameUI(UISurface, std::move(draw), error))
            return;
        RenderSurfaceSnapshot surface;
        if (app.GetEditorPreview(UISurface, surface) && surface.textureToken) {
            const float width = std::min(ImGui::GetContentRegionAvail().x, 640.0f);
            const ImVec2 size{width, width * 360 / 640};
            const auto start = ImGui::GetCursorScreenPos();
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(surface.textureToken)), size);
            const auto& io = ImGui::GetIO();
            const bool hovered = ImGui::IsItemHovered();
            const bool focused = ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() &&
                                 !io.WantTextInput && !io.AppFocusLost;
            const int modifiers = (io.KeyCtrl ? 1 : 0) | (io.KeyShift ? 2 : 0) | (io.KeyAlt ? 4 : 0) |
                                  (io.KeySuper ? 8 : 0);
            if (!focused && (m_PointerCaptured || m_Runtime->HasFocus())) {
                UIInputEvent blur;
                blur.type = UIInputEvent::Type::Blur;
                bool ignored = false;
                if (!m_Runtime->Input(blur, ignored, error))
                    return;
                m_PointerCaptured = false;
            }
            // 拖出图像后仍交付释放事件；否则 RmlUi 会一直保留按下/拖选状态。
            if ((hovered || m_PointerCaptured) && size.x > 0 && size.y > 0) {
                const auto mouse = io.MousePos;
                bool consumed = false;
                UIInputEvent event;
                event.type = UIInputEvent::Type::Move;
                event.modifiers = modifiers;
                event.x = (mouse.x - start.x) * 640 / size.x;
                event.y = (mouse.y - start.y) * 360 / size.y;
                if (!m_Runtime->Input(event, consumed, error))
                    return;
                for (int i = 0; i < 3; ++i)
                    if ((hovered && ImGui::IsMouseClicked(i)) || ImGui::IsMouseReleased(i)) {
                        const bool down = hovered && ImGui::IsMouseClicked(i);
                        event.type = down ? UIInputEvent::Type::ButtonDown : UIInputEvent::Type::ButtonUp;
                        event.key = i;
                        if (!m_Runtime->Input(event, consumed, error))
                            return;
                        m_PointerCaptured |= down;
                    }
                if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2))
                    m_PointerCaptured = false;
                if (hovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
                    event.type = UIInputEvent::Type::Wheel;
                    event.x = -io.MouseWheelH;
                    event.y = -io.MouseWheel;
                    if (!m_Runtime->Input(event, consumed, error))
                        return;
                }
            }
            if (focused && m_Runtime->HasFocus()) {
                const auto key = [&](ImGuiKey imgui, int platform) {
                    if (!ImGui::IsKeyPressed(imgui, true) && !ImGui::IsKeyReleased(imgui))
                        return true;
                    UIInputEvent input;
                    input.type =
                        ImGui::IsKeyReleased(imgui) ? UIInputEvent::Type::KeyUp : UIInputEvent::Type::KeyDown;
                    input.key = UIInputRouter::TranslatePlatformKey(platform);
                    input.modifiers = modifiers;
                    bool consumed = false;
                    return m_Runtime->Input(input, consumed, error);
                };
                for (int i = 0; i < 26; ++i)
                    if (!key(ImGuiKey(ImGuiKey_A + i), Key::A + i))
                        return;
                for (int i = 0; i < 10; ++i)
                    if (!key(ImGuiKey(ImGuiKey_0 + i), Key::D0 + i))
                        return;
                const std::pair<ImGuiKey, int> navigation[]{
                    {ImGuiKey_Space, Key::Space},         {ImGuiKey_Escape, Key::Escape},
                    {ImGuiKey_Enter, Key::Enter},         {ImGuiKey_Tab, Key::Tab},
                    {ImGuiKey_Backspace, Key::Backspace}, {ImGuiKey_Delete, Key::Delete},
                    {ImGuiKey_LeftArrow, Key::Left},      {ImGuiKey_RightArrow, Key::Right},
                    {ImGuiKey_UpArrow, Key::Up},           {ImGuiKey_DownArrow, Key::Down},
                    {ImGuiKey_Home, Key::Home},           {ImGuiKey_End, Key::End}};
                for (const auto& [imgui, platform] : navigation)
                    if (!key(imgui, platform))
                        return;
                for (const auto codepoint : io.InputQueueCharacters) {
                    UIInputEvent input;
                    input.type = UIInputEvent::Type::Text;
                    input.codepoint = uint32_t(codepoint);
                    input.modifiers = modifiers;
                    bool consumed = false;
                    if (!m_Runtime->Input(input, consumed, error))
                        return;
                }
            }
            const auto caret = m_Runtime->TextInput();
            if (focused && caret.active) {
                if (ImGui::GetWindowViewport()->PlatformHandle == app.GetWindow().GetNativeWindow()) {
                    TextInputSnapshot region = caret;
                    const auto main = ImGui::GetMainViewport()->Pos;
                    region.x = start.x - main.x + caret.x * size.x / 640;
                    region.y = start.y - main.y + caret.y * size.y / 360;
                    region.width = caret.width * size.x / 640;
                    region.height = caret.height * size.y / 360;
                    const std::weak_ptr<UIRuntime> weak = m_Runtime;
                    if (!app.GetWindow().SetTextInputTarget(
                        UISurface, region,
                        [weak](const TextComposition& composition) {
                            if (const auto runtime = weak.lock()) {
                                UIInputEvent input;
                                input.type = UIInputEvent::Type::Composition;
                                input.composition = composition;
                                bool consumed = false;
                                std::string error;
                                if (!runtime->Input(input, consumed, error) && !error.empty())
                                    CAT_CORE_WARN("Game UI preview composition: {0}", error);
                            }
                        },
                        error))
                        return;
                } else
                    app.GetWindow().ClearTextInputTarget(UISurface);
            } else {
                app.GetWindow().ClearTextInputTarget(UISurface);
                if (caret.active) {
                    UIInputEvent blur;
                    blur.type = UIInputEvent::Type::Blur;
                    bool ignored = false;
                    m_Runtime->Input(blur, ignored, error);
                }
            }
        }
        for (const auto& event : m_Runtime->TakeEvents())
            ImGui::Text("%s: %s %s", event.element.c_str(), event.type.c_str(), event.value.c_str());
    }
} // namespace Catalpa::Editor
