#include "AssetEditors/AudioUIInspector.h"
#include "Commands/EditorCommand.h"
#include "Core/Application.h"
#include "Assets/AssetManager.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/AudioSourceComponent.h"
#include "Scene/Components/AudioListenerComponent.h"
#include "Scene/Components/UIComponent.h"
#include "UI/UIDocumentAsset.h"
#include <imgui.h>

namespace Catalpa::Editor {
    namespace {
        template <class T> bool SamePin(const AssetVersionRef<T>& a, const AssetVersionRef<T>& b) {
            return a.GetId() == b.GetId() && a.GetVersion() == b.GetVersion() &&
                   a.GetShared() == b.GetShared();
        }
        bool Same(const AudioSourceComponent& a, const AudioSourceComponent& b) {
            const auto& x = a.settings;
            const auto& y = b.settings;
            return a.clip == b.clip && a.enabled == b.enabled && a.autoPlay == b.autoPlay &&
                   SamePin(a.clipVersion, b.clipVersion) && x.gain == y.gain && x.pitch == y.pitch &&
                   x.loop == y.loop && x.spatial == y.spatial && x.minDistance == y.minDistance &&
                   x.maxDistance == y.maxDistance && x.position == y.position;
        }
        bool Same(const AudioListenerComponent& a, const AudioListenerComponent& b) {
            return a.enabled == b.enabled;
        }
        bool Same(const UIComponent& a, const UIComponent& b) {
            return a.document == b.document && a.enabled == b.enabled && a.order == b.order &&
                   SamePin(a.documentVersion, b.documentVersion);
        }
        bool Valid(const AudioSourceComponent& source, std::string& error) {
            const auto& s = source.settings;
            if (!source.clip.HasValue() || !source.clipVersion.HasValue() ||
                source.clipVersion.GetId() != source.clip || !std::isfinite(s.gain) || s.gain < 0 ||
                s.gain > 16 || !std::isfinite(s.pitch) || s.pitch < .125f || s.pitch > 8 ||
                !std::isfinite(s.minDistance) || s.minDistance <= 0 || !std::isfinite(s.maxDistance) ||
                s.maxDistance < s.minDistance) {
                error = "Audio source requires a loaded exact clip and finite gain/pitch/distance parameters";
                return false;
            }
            return true;
        }
        bool Valid(const AudioListenerComponent&, std::string&) {
            return true;
        }
        bool Valid(const UIComponent& ui, std::string& error) {
            if (!ui.document.HasValue() || !ui.documentVersion.HasValue() ||
                ui.documentVersion.GetId() != ui.document || ui.order < -32768 || ui.order > 32767) {
                error = "Game UI requires a loaded exact document and order in [-32768,32767]";
                return false;
            }
            return true;
        }
        template <class T> class ComponentCommand final : public EditorCommand {
          public:
            ComponentCommand(StableId owner, EntityReference target, std::optional<T> before,
                             std::optional<T> after)
                : EditorCommand(owner), m_Target(target), m_Before(std::move(before)),
                  m_After(std::move(after)) {}
            bool Apply(Document& doc, std::string& error) override {
                return Run(doc, m_Before, m_After, error);
            }
            bool Revert(Document& doc, std::string& error) override {
                return Run(doc, m_After, m_Before, error);
            }

          private:
            bool Run(Document& document, const std::optional<T>& expected, const std::optional<T>& next,
                     std::string& error) {
                auto* doc = dynamic_cast<SceneDocument*>(&document);
                auto* scene = doc ? doc->Resolve() : nullptr;
                if (!scene || doc->Id() != Owner() || doc->Playing() || !scene->CanEditNow() ||
                    scene->GetWorld().GetIdentity() != m_Target.world ||
                    !doc->CheckTarget(m_Target.persistent, error)) {
                    error = "Audio/UI author target is unavailable or playing";
                    return false;
                }
                // 文档只允许受控 Delete/Undo 重绑实体代次；重新加载的另一 World 仍被上方身份检查拒绝。
                const auto entity = scene->Resolve(m_Target.persistent);
                const auto& world = scene->GetWorld();
                const bool exists = world.Has<T>(entity);
                T actual;
                if (exists != bool(expected) ||
                    (exists && (!world.Get(entity, actual).IsSuccess() || !Same(actual, *expected)))) {
                    error = "Audio/UI component changed outside its document command";
                    return false;
                }
                if (next && !Valid(*next, error))
                    return false;
                if constexpr (std::is_same_v<T, AudioListenerComponent>)
                    if (next && next->enabled) {
                        size_t listeners = 0;
                        const auto inspected = world.ForEach([&](EntityId other) {
                            if (other == entity || !world.Has<AudioListenerComponent>(other))
                                return WorldStatus{};
                            AudioListenerComponent listener;
                            const auto status = world.Get(other, listener);
                            if (status.IsSuccess() && listener.enabled)
                                ++listeners;
                            return status;
                        });
                        if (!inspected.IsSuccess() || listeners) {
                            error = "Disable the other active audio listener before enabling this one";
                            return false;
                        }
                    }
                const auto queued =
                    QueueDynamicComponent(*scene, entity, [entity, next](World& target) -> WorldStatus {
                        if (next)
                            return target.Has<T>(entity) ? target.Set(entity, *next)
                                                         : target.Add(entity, *next);
                        return target.Remove<T>(entity);
                    });
                if (!queued.IsSuccess()) {
                    error = queued.message;
                    return false;
                }
                const auto status = scene->CommitPending();
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                error.clear();
                return true;
            }
            EntityReference m_Target;
            std::optional<T> m_Before, m_After;
        };
        template <class T>
        bool Edit(SceneDocument& doc, EntityId entity, std::optional<T> value, std::string& error) {
            auto* scene = doc.Resolve();
            if (!scene) {
                error = "Audio/UI author Scene expired";
                return false;
            }
            std::optional<T> previous;
            if (scene->GetWorld().Has<T>(entity)) {
                previous.emplace();
                const auto status = scene->GetWorld().Get(entity, *previous);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
            }
            return doc.History().Execute(
                doc,
                std::make_unique<ComponentCommand<T>>(doc.Id(), scene->MakeReference(entity),
                                                      std::move(previous), std::move(value)),
                error);
        }
        bool AssetInput(const char* label, AssetId& id, std::string& error) {
            struct Draft {
                AssetId original;
                std::array<char, 64> text{};
                bool initialized = false;
            };
            static std::map<ImGuiID, Draft> drafts;
            const auto key = ImGui::GetID(label);
            if (!drafts.contains(key) && drafts.size() >= 256)
                drafts.clear();
            auto& draft = drafts[key];
            if (!draft.initialized || draft.original != id) {
                draft = {};
                draft.original = id;
                draft.initialized = true;
                const auto text = id.HasValue() ? id.ToString() : std::string{};
                std::copy(text.begin(), text.end(), draft.text.begin());
            }
            if (!ImGui::InputText(label, draft.text.data(), draft.text.size(),
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                return false;
            AssetId candidate;
            if (!AssetId::TryParse(draft.text.data(), candidate)) {
                error = "Enter a cooked asset UUID and press Enter";
                return false;
            }
            id = candidate;
            draft.original = id;
            return true;
        }
        template <class T>
        bool Pin(AssetManager& assets, AssetId id, AssetVersionRef<T>& output, std::string& error) {
            if (output.HasValue() && output.GetId() == id)
                return true;
            bool fallback = false;
            if (assets.Acquire(id, output, fallback, error) && !fallback)
                return true;
            if (!assets.Request(id, error))
                return false;
            error = "Asset is loading; retry the component edit once its Cooked version is ready";
            return false;
        }
    } // namespace

    void DrawAudioUIInspector(Application& app, SceneDocument& doc, EntityId entity, const PanelText& text,
                              std::string& error) try {
        auto* scene = doc.Resolve();
        if (!scene || doc.Playing() || !scene->GetWorld().IsAlive(entity))
            return;
        const auto label = [&](const char* key, const char* fallback) { return text(key, fallback, key); };
        const auto& world = scene->GetWorld();
        ImGui::PushID("AudioUIComponents");
        struct Scope {
            ~Scope() {
                ImGui::PopID();
            }
        } scope;
        if (world.Has<AudioSourceComponent>(entity)) {
            AudioSourceComponent source;
            if (!world.Get(entity, source).IsSuccess())
                return;
            if (ImGui::CollapsingHeader(label("audio.source", "Audio source").c_str())) {
                bool changed =
                    AssetInput(label("audio.clip_id", "Clip asset UUID").c_str(), source.clip, error);
                changed |= ImGui::Checkbox(label("audio.enabled", "Enabled").c_str(), &source.enabled);
                changed |= ImGui::Checkbox(label("audio.auto_play", "Auto play").c_str(), &source.autoPlay);
                changed |=
                    ImGui::DragFloat(label("audio.gain", "Gain").c_str(), &source.settings.gain, .01f, 0, 16);
                changed |= ImGui::DragFloat(label("audio.pitch", "Pitch").c_str(), &source.settings.pitch,
                                            .01f, .125f, 8);
                changed |= ImGui::Checkbox(label("audio.loop", "Loop").c_str(), &source.settings.loop);
                changed |= ImGui::Checkbox(label("audio.spatial", "Spatial audio").c_str(),
                                           &source.settings.spatial);
                changed |= ImGui::DragFloat(label("audio.min_distance", "Minimum distance (m)").c_str(),
                                            &source.settings.minDistance, .05f, .01f, 10000);
                changed |= ImGui::DragFloat(label("audio.max_distance", "Maximum distance (m)").c_str(),
                                            &source.settings.maxDistance, .1f, .01f, 100000);
                if (changed && Pin(app.GetAssets(), source.clip, source.clipVersion, error))
                    Edit(doc, entity, std::optional(source), error);
                if (ImGui::Button(label("audio.remove_source", "Remove audio source").c_str()))
                    Edit<AudioSourceComponent>(doc, entity, {}, error);
            }
        } else {
            static AssetId clip;
            AssetInput(label("audio.new_clip", "Clip UUID for new audio source").c_str(), clip, error);
            if (clip.HasValue() && ImGui::Button(label("audio.add_source", "Add audio source").c_str())) {
                AudioSourceComponent source;
                source.clip = clip;
                if (Pin(app.GetAssets(), clip, source.clipVersion, error))
                    Edit(doc, entity, std::optional(source), error);
            }
        }
        if (world.Has<AudioListenerComponent>(entity)) {
            AudioListenerComponent listener;
            if (!world.Get(entity, listener).IsSuccess())
                return;
            if (ImGui::Checkbox(label("audio.listener_enabled", "Audio listener enabled").c_str(),
                                &listener.enabled))
                Edit(doc, entity, std::optional(listener), error);
            if (ImGui::Button(label("audio.remove_listener", "Remove audio listener").c_str()))
                Edit<AudioListenerComponent>(doc, entity, {}, error);
        } else if (ImGui::Button(label("audio.add_listener", "Add audio listener").c_str()))
            Edit(doc, entity, std::optional(AudioListenerComponent{}), error);

        if (world.Has<UIComponent>(entity)) {
            UIComponent ui;
            if (!world.Get(entity, ui).IsSuccess())
                return;
            if (ImGui::CollapsingHeader(label("ui.component", "Game UI").c_str())) {
                bool changed =
                    AssetInput(label("ui.document_id", "UI document asset UUID").c_str(), ui.document, error);
                changed |= ImGui::Checkbox(label("ui.enabled", "Game UI enabled").c_str(), &ui.enabled);
                int order = ui.order;
                changed |= ImGui::InputInt(label("ui.order", "Draw order").c_str(), &order);
                ui.order = order;
                if (changed && Pin(app.GetAssets(), ui.document, ui.documentVersion, error))
                    Edit(doc, entity, std::optional(ui), error);
                if (ImGui::Button(label("ui.remove_component", "Remove Game UI").c_str()))
                    Edit<UIComponent>(doc, entity, {}, error);
            }
        } else {
            static AssetId document;
            AssetInput(label("ui.new_document", "Document UUID for new Game UI").c_str(), document, error);
            if (document.HasValue() && ImGui::Button(label("ui.add_component", "Add Game UI").c_str())) {
                UIComponent ui;
                ui.document = document;
                if (Pin(app.GetAssets(), document, ui.documentVersion, error))
                    Edit(doc, entity, std::optional(ui), error);
            }
        }
    } catch (const std::exception& exception) {
        error = exception.what();
    }
} // namespace Catalpa::Editor
