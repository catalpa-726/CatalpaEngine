#include "catpch.h"
#include "Scene/AudioUIComponents.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/AudioSourceComponent.h"
#include "Scene/Components/AudioListenerComponent.h"
#include "Scene/Components/UIComponent.h"
#include "ECS/Detail/WorldComponentType.h"
#include "Core/Serialization/AuthorJson.h"
#include "UI/UIDocumentAsset.h"

namespace Catalpa {
    const ComponentType& ComponentTraits<AudioSourceComponent>::Get() {
        return Detail::MakeComponentType<AudioSourceComponent>();
    }
    const ComponentType& ComponentTraits<AudioListenerComponent>::Get() {
        return Detail::MakeComponentType<AudioListenerComponent>();
    }
    const ComponentType& ComponentTraits<UIComponent>::Get() {
        return Detail::MakeComponentType<UIComponent>();
    }
    namespace {
        using Json = AuthorJson::Json;
        Json ReadValue(const ArchiveValue& value) {
            std::string text;
            ArchiveError error;
            if (!JsonArchive::Write(value, text, error))
                throw std::invalid_argument(error.ToString());
            return AuthorJson::Parse(text, 65536);
        }
        ArchiveValue Archive(const Json& value) {
            std::unique_ptr<ArchiveValue> output;
            ArchiveError error;
            if (!JsonArchive::Parse(value.dump(), output, error))
                throw std::invalid_argument(error.ToString());
            return std::move(*output);
        }
        AudioSourceComponent ReadSource(const Json& value) {
            AuthorJson::Fields(value, {"clip", "enabled", "autoPlay", "gain", "pitch", "loop", "spatial",
                                       "minDistance", "maxDistance"});
            AudioSourceComponent result;
            result.clip = AuthorJson::Identity<AssetId>(value.at("clip"));
            result.enabled = value.at("enabled").get<bool>();
            result.autoPlay = value.at("autoPlay").get<bool>();
            auto& s = result.settings;
            s.gain = float(AuthorJson::Number(value.at("gain")));
            s.pitch = float(AuthorJson::Number(value.at("pitch")));
            s.loop = value.at("loop").get<bool>();
            s.spatial = value.at("spatial").get<bool>();
            s.minDistance = float(AuthorJson::Number(value.at("minDistance")));
            s.maxDistance = float(AuthorJson::Number(value.at("maxDistance")));
            if (!std::isfinite(s.gain) || !std::isfinite(s.pitch) || !std::isfinite(s.minDistance) ||
                !std::isfinite(s.maxDistance) || s.gain < 0 || s.gain > 16 || s.pitch < 0.125f ||
                s.pitch > 8 || s.minDistance <= 0 || s.maxDistance < s.minDistance)
                throw std::invalid_argument("Audio source gain/pitch/distance rejected");
            return result;
        }
        UIComponent ReadUI(const Json& value) {
            AuthorJson::Fields(value, {"document", "enabled", "order"});
            UIComponent result;
            result.document = AuthorJson::Identity<AssetId>(value.at("document"));
            result.enabled = value.at("enabled").get<bool>();
            if (!value.at("order").is_number_integer())
                throw std::invalid_argument("UI order must be an integer");
            const auto order = value.at("order").get<int64_t>();
            if (order < -32768 || order > 32767)
                throw std::invalid_argument("UI order rejected");
            result.order = int32_t(order);
            return result;
        }
        template <class T> WorldStatus Queue(Scene& scene, EntityId entity, T value) {
            return QueueDynamicComponent(scene, entity, [entity, value = std::move(value)](World& world) {
                return world.Has<T>(entity) ? world.Set(entity, value) : world.Add(entity, value);
            });
        }
    } // namespace
    bool CaptureAudioUIComponents(const Scene& scene, EntityId entity, ArchiveValue::Object& out,
                                  std::string& error) {
        try {
            ArchiveValue::Object result;
            const auto& world = scene.GetWorld();
            if (world.Has<AudioSourceComponent>(entity)) {
                AudioSourceComponent source;
                const auto status = world.Get(entity, source);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                const auto& s = source.settings;
                result.emplace("audio_source", Archive(Json{{"clip", source.clip.ToString()},
                                                            {"enabled", source.enabled},
                                                            {"autoPlay", source.autoPlay},
                                                            {"gain", s.gain},
                                                            {"pitch", s.pitch},
                                                            {"loop", s.loop},
                                                            {"spatial", s.spatial},
                                                            {"minDistance", s.minDistance},
                                                            {"maxDistance", s.maxDistance}}));
            }
            if (world.Has<AudioListenerComponent>(entity)) {
                AudioListenerComponent listener;
                const auto status = world.Get(entity, listener);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                result.emplace("audio_listener", Archive(Json{{"enabled", listener.enabled}}));
            }
            if (world.Has<UIComponent>(entity)) {
                UIComponent ui;
                const auto status = world.Get(entity, ui);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                result.emplace("ui", Archive(Json{{"document", ui.document.ToString()},
                                                  {"enabled", ui.enabled},
                                                  {"order", ui.order}}));
            }
            for (auto& [key, value] : result)
                if (!out.emplace(key, std::move(value)).second) {
                    error = "Duplicate dynamic component: " + key;
                    return false;
                }
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    bool ValidateAudioUIComponent(std::string_view key, const ArchiveValue& value, std::string& error) {
        try {
            const auto json = ReadValue(value);
            if (key == "audio_source")
                ReadSource(json);
            else if (key == "ui")
                ReadUI(json);
            else if (key == "audio_listener") {
                AuthorJson::Fields(json, {"enabled"});
                json.at("enabled").get<bool>();
            } else {
                error = "Unknown audio/UI component: " + std::string(key);
                return false;
            }
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    bool AudioUIComponentDependencies(std::string_view key, const ArchiveValue& value,
                                      std::vector<AssetId>& out, std::string& error) {
        if (!ValidateAudioUIComponent(key, value, error))
            return false;
        try {
            const auto json = ReadValue(value);
            if (key == "audio_source")
                out.push_back(ReadSource(json).clip);
            else if (key == "ui")
                out.push_back(ReadUI(json).document);
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    WorldStatus RestoreAudioUIComponent(Scene& scene, EntityId entity, std::string_view key,
                                        const ArchiveValue& value, std::string& error) {
        if (!ValidateAudioUIComponent(key, value, error))
            return {WorldError::InvalidArgument, "Invalid audio/UI component"};
        try {
            const auto json = ReadValue(value);
            if (key == "audio_source")
                return Queue(scene, entity, ReadSource(json));
            if (key == "ui")
                return Queue(scene, entity, ReadUI(json));
            return Queue(scene, entity, AudioListenerComponent{json.at("enabled").get<bool>()});
        } catch (const std::exception& e) {
            error = e.what();
            return {WorldError::InvalidArgument, "Invalid audio/UI component"};
        }
    }
    WorldStatus RestoreAudioUIComponent(World& world, EntityId entity, std::string_view key,
                                        const ArchiveValue& value, std::string& error) {
        if (!ValidateAudioUIComponent(key, value, error))
            return {WorldError::InvalidArgument, "Invalid audio/UI component"};
        try {
            const auto json = ReadValue(value);
            if (key == "audio_source") {
                const auto source = ReadSource(json);
                return world.Has<AudioSourceComponent>(entity) ? world.Set(entity, source)
                                                               : world.Add(entity, source);
            }
            if (key == "ui") {
                const auto ui = ReadUI(json);
                return world.Has<UIComponent>(entity) ? world.Set(entity, ui) : world.Add(entity, ui);
            }
            const AudioListenerComponent listener{json.at("enabled").get<bool>()};
            return world.Has<AudioListenerComponent>(entity) ? world.Set(entity, listener)
                                                             : world.Add(entity, listener);
        } catch (const std::exception& e) {
            error = e.what();
            return {WorldError::InvalidArgument, "Invalid audio/UI component"};
        }
    }
    bool BindAudioUIComponentVersions(World& world, EntityId entity,
                                      std::span<const AssetDependencyValue> versions, std::string& error) {
        if (versions.empty()) {
            error.clear();
            return true;
        }
        const auto bind = [&]<class T>(AssetId id, AssetVersionRef<T>& output) {
            for (const auto& value : versions)
                if (value.id == id && value.cppType == std::type_index(typeid(T)))
                    return AssetVersionRef<T>::TryCreate(
                        id, value.version, std::static_pointer_cast<const T>(value.value), output, error);
            error = "Missing exact audio/UI dependency version: " + id.ToString();
            return false;
        };
        if (world.Has<AudioSourceComponent>(entity)) {
            AudioSourceComponent source;
            auto status = world.Get(entity, source);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (!bind(source.clip, source.clipVersion))
                return false;
            status = world.Set(entity, source);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        if (world.Has<UIComponent>(entity)) {
            UIComponent ui;
            auto status = world.Get(entity, ui);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (!bind(ui.document, ui.documentVersion))
                return false;
            status = world.Set(entity, ui);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        error.clear();
        return true;
    }
    bool CollectAudioUIComponentVersions(const World& world, EntityId entity,
                                         std::vector<AssetDependencyValue>& output, std::string& error) {
        if (world.Has<AudioSourceComponent>(entity)) {
            AudioSourceComponent source;
            const auto status = world.Get(entity, source);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (source.clipVersion.HasValue()) {
                if (source.clipVersion.GetId() != source.clip) {
                    error = "Audio component pin does not match author identity";
                    return false;
                }
                output.push_back({source.clip, source.clipVersion.GetVersion(),
                                  source.clipVersion.GetShared(), typeid(AudioClipAsset)});
            }
        }
        if (world.Has<UIComponent>(entity)) {
            UIComponent ui;
            const auto status = world.Get(entity, ui);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (ui.documentVersion.HasValue()) {
                if (ui.documentVersion.GetId() != ui.document) {
                    error = "UI component pin does not match author identity";
                    return false;
                }
                output.push_back({ui.document, ui.documentVersion.GetVersion(),
                                  ui.documentVersion.GetShared(), typeid(UIDocumentAsset)});
            }
        }
        error.clear();
        return true;
    }
} // namespace Catalpa
