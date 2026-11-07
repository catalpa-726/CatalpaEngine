#include "catpch.h"
#include "Runtime/Systems/UISystem.h"
#include "Scene/Scene.h"
#include "Scene/Components/UIComponent.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
    struct UISystem::Impl {
        struct Entry {
            AssetId id;
            uint64_t version = 0;
            int order = 0;
            std::unique_ptr<UIRuntime> runtime;
            double startSeconds = 0;
        };
        SceneHandle scene;
        uint64_t world = 0;
        std::map<PersistentEntityId, Entry> entries;
        std::vector<PersistentEntityId> order;
        std::shared_ptr<const UIDrawData> frame;
        UIRuntimeConfig config;
        uint64_t nextEvent = 1;
        std::string eventDiagnostic;
    };
    UISystem::UISystem() : m_Impl(std::make_unique<Impl>()) {}
    UISystem::~UISystem() {
        Shutdown();
    }
    bool UISystem::Configure(UIRuntimeConfig config, std::string& error) {
        if (!std::isfinite(config.density) || config.density < .5f || config.density > 4 ||
            config.maxDocuments < 1 || config.maxDocuments > 16) {
            error = "UI runtime configuration rejected";
            return false;
        }
        m_Impl->config = config;
        error.clear();
        return true;
    }
    bool UISystem::Update(SceneHandle handle, const Scene* scene, AssetManager& assets, uint32_t width,
                          uint32_t height, double elapsed, std::string& error) {
        if (!scene || handle != m_Impl->scene || scene->GetWorld().GetIdentity() != m_Impl->world) {
            Shutdown();
            m_Impl->scene = handle;
            m_Impl->world = scene ? scene->GetWorld().GetIdentity() : 0;
        }
        if (!scene || !width || !height) {
            m_Impl->frame.reset();
            error.clear();
            return true;
        }
        if (!m_Impl->eventDiagnostic.empty()) {
            error = std::exchange(m_Impl->eventDiagnostic, {});
            return false;
        }
        std::vector<std::pair<PersistentEntityId, UIComponent>> values;
        const auto status = scene->GetWorld().ForEach([&](EntityId entity) {
            if (!scene->GetWorld().Has<UIComponent>(entity))
                return WorldStatus{};
            UIComponent ui;
            IdentityComponent id;
            auto result = scene->GetWorld().Get(entity, ui);
            if (!result.IsSuccess())
                return result;
            result = scene->GetIdentity(entity, id);
            if (!result.IsSuccess())
                return result;
            if (ui.enabled)
                values.emplace_back(id.persistent, ui);
            return WorldStatus{};
        });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (values.size() > m_Impl->config.maxDocuments) {
            error = "Scene UI document count exceeds configured limit";
            return false;
        }
        std::set<PersistentEntityId> live;
        for (const auto& [id, ui] : values) {
            live.insert(id);
            AssetVersionRef<UIDocumentAsset> asset = ui.documentVersion;
            bool fallback = false;
            if (asset.HasValue() && asset.GetId() != ui.document) {
                error = "UI component pin identity mismatch";
                return false;
            }
            if (!asset.HasValue() && !assets.Acquire(ui.document, asset, fallback, error)) {
                if (!assets.Request(ui.document, error))
                    return false;
                error.clear();
                continue;
            }
            auto found = m_Impl->entries.find(id);
            if (found == m_Impl->entries.end() || found->second.id != ui.document ||
                found->second.version != asset.GetVersion()) {
                auto runtime = std::make_unique<UIRuntime>();
                // 新版本完全创建成功后才替换旧上下文；失败时保留旧版本及旧绘制快照。
                if (!runtime->Initialize(asset, width, height, error))
                    return false;
                Impl::Entry entry{ui.document, asset.GetVersion(), ui.order, std::move(runtime), elapsed};
                m_Impl->entries.insert_or_assign(id, std::move(entry));
            }
            m_Impl->entries.at(id).order = ui.order;
        }
        for (auto it = m_Impl->entries.begin(); it != m_Impl->entries.end();)
            if (!live.contains(it->first))
                it = m_Impl->entries.erase(it);
            else
                ++it;
        m_Impl->order.clear();
        for (const auto& [id, entry] : m_Impl->entries)
            m_Impl->order.push_back(id);
        std::stable_sort(m_Impl->order.begin(), m_Impl->order.end(), [&](auto a, auto b) {
            return m_Impl->entries.at(a).order < m_Impl->entries.at(b).order;
        });
        auto frame = std::make_shared<UIDrawData>();
        frame->width = width;
        frame->height = height;
        for (const auto id : m_Impl->order) {
            auto& runtime = *m_Impl->entries.at(id).runtime;
            std::shared_ptr<const UIDrawData> draw;
            const auto localSeconds = elapsed - m_Impl->entries.at(id).startSeconds;
            if (!runtime.Resize(width, height, error) || !runtime.SetDensity(m_Impl->config.density, error) ||
                !runtime.Advance(localSeconds, error) || !runtime.Render(draw, error))
                return false;
            if (draw->vertices.size() > 1048576 - frame->vertices.size() ||
                draw->indices.size() > 3145728 - frame->indices.size() ||
                draw->batches.size() > 4096 - frame->batches.size()) {
                error = "Combined scene UI geometry exceeds its budget";
                return false;
            }
            const auto vertex = int32_t(frame->vertices.size());
            const auto index = uint32_t(frame->indices.size());
            frame->vertices.insert(frame->vertices.end(), draw->vertices.begin(), draw->vertices.end());
            frame->indices.insert(frame->indices.end(), draw->indices.begin(), draw->indices.end());
            for (auto batch : draw->batches) {
                batch.baseVertex += vertex;
                batch.firstIndex += index;
                frame->batches.push_back(std::move(batch));
            }
        }
        m_Impl->frame = std::move(frame);
        error.clear();
        return true;
    }
    bool UISystem::Input(const UIInputEvent& event, bool& consumed, std::string& error) {
        consumed = false;
        const bool keyboard =
            event.type == UIInputEvent::Type::KeyDown || event.type == UIInputEvent::Type::KeyUp ||
            event.type == UIInputEvent::Type::Text || event.type == UIInputEvent::Type::Composition;
        for (auto it = m_Impl->order.rbegin(); it != m_Impl->order.rend(); ++it) {
            auto& runtime = *m_Impl->entries.at(*it).runtime;
            if (keyboard && !runtime.HasFocus())
                continue;
            bool handled = false;
            if (!runtime.Input(event, handled, error))
                return false;
            if (event.type == UIInputEvent::Type::Blur || event.type == UIInputEvent::Type::ButtonUp) {
                consumed |= handled;
                continue;
            }
            if (handled) {
                consumed = true;
                if (event.type == UIInputEvent::Type::ButtonDown)
                    for (const auto& [id, entry] : m_Impl->entries)
                        if (id != *it) {
                            UIInputEvent blur;
                            blur.type = UIInputEvent::Type::Blur;
                            bool ignored = false;
                            if (!entry.runtime->Input(blur, ignored, error))
                                return false;
                        }
                break;
            }
        }
        error.clear();
        return true;
    }
    bool UISystem::SetValue(PersistentEntityId entity, std::string_view id, std::string_view value,
                            std::string& error) {
        const auto it = m_Impl->entries.find(entity);
        if (it == m_Impl->entries.end()) {
            error = "UI entity has no live document";
            return false;
        }
        return it->second.runtime->SetValue(id, value, error);
    }
    bool UISystem::HasFocus() const {
        for (const auto& [id, entry] : m_Impl->entries)
            if (entry.runtime->HasFocus())
                return true;
        return false;
    }
    TextInputSnapshot UISystem::TextInput() const {
        for (auto it = m_Impl->order.rbegin(); it != m_Impl->order.rend(); ++it) {
            const auto state = m_Impl->entries.at(*it).runtime->TextInput();
            if (state.active)
                return state;
        }
        return {};
    }
    std::vector<UIEvent> UISystem::TakeEvents() {
        std::vector<UIEvent> events;
        for (const auto id : m_Impl->order)
            for (auto& event : m_Impl->entries.at(id).runtime->TakeEvents()) {
                if (events.size() >= 256 || m_Impl->nextEvent == UINT64_MAX) {
                    m_Impl->eventDiagnostic =
                        "Combined scene UI event budget exhausted; excess events rejected";
                    continue;
                }
                event.sequence = m_Impl->nextEvent++;
                event.world = m_Impl->world;
                event.source = id;
                events.push_back(std::move(event));
            }
        return events;
    }
    const std::shared_ptr<const UIDrawData>& UISystem::DrawData() const noexcept {
        return m_Impl->frame;
    }
    void UISystem::Shutdown() noexcept {
        // Renderer 已持有的快照继续拥有纹理版本；移除 context 不使在途绘制数据悬空。
        m_Impl->frame.reset();
        m_Impl->entries.clear();
        m_Impl->order.clear();
        m_Impl->scene = {};
        m_Impl->world = 0;
        m_Impl->eventDiagnostic.clear();
    }
} // namespace Catalpa
