#include "catpch.h"
#include "Runtime/Systems/AudioExtractSystem.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/AudioSourceComponent.h"
#include "Scene/Components/AudioListenerComponent.h"

namespace Catalpa {
    bool AudioExtractSystem::Update(SceneHandle handle, const Scene* scene, AssetManager& assets,
                                    AudioService& audio, std::string& error) {
        if (!scene || handle != m_Scene || scene->GetWorld().GetIdentity() != m_World) {
            Stop(audio);
            m_Scene = handle;
            m_World = scene ? scene->GetWorld().GetIdentity() : 0;
        }
        if (!PumpRetiring(audio, error))
            return false;
        if (!scene || m_World == m_StoppingWorld) {
            error.clear();
            return true;
        }
        m_StoppingWorld = 0;
        std::vector<AudioListenerState> listeners;
        struct SourceValue {
            PersistentEntityId id;
            AudioSourceComponent source;
        };
        std::vector<SourceValue> sources;
        const auto status = scene->GetWorld().ForEach([&](EntityId entity) {
            TransformComponent transform;
            if (!scene->GetTransform(entity, transform).IsSuccess() || !transform.worldValid)
                return WorldStatus{};
            const auto& world = scene->GetWorld();
            if (world.Has<AudioListenerComponent>(entity)) {
                AudioListenerComponent listener;
                auto result = world.Get(entity, listener);
                if (!result.IsSuccess())
                    return result;
                if (listener.enabled)
                    listeners.push_back({Math::Vec3(transform.worldMatrix[3]),
                                         -Math::Vec3(transform.worldMatrix[2]),
                                         Math::Vec3(transform.worldMatrix[1])});
            }
            if (world.Has<AudioSourceComponent>(entity)) {
                AudioSourceComponent source;
                IdentityComponent identity;
                auto result = world.Get(entity, source);
                if (!result.IsSuccess())
                    return result;
                result = scene->GetIdentity(entity, identity);
                if (!result.IsSuccess())
                    return result;
                if (source.enabled && source.autoPlay) {
                    source.settings.position = Math::Vec3(transform.worldMatrix[3]);
                    sources.push_back({identity.persistent, source});
                }
            }
            return WorldStatus{};
        });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (listeners.size() > 1) {
            error = "Audio scene contains more than one enabled listener";
            return false;
        }
        if (!audio.SetListener(listeners.empty() ? AudioListenerState{} : listeners.front(), error))
            return false;
        std::set<PersistentEntityId> live;
        for (const auto& item : sources) {
            live.insert(item.id);
            auto found = m_Sources.find(item.id);
            const auto sameSettings = [&](const AudioVoiceSettings& old) {
                const auto& value = item.source.settings;
                return old.gain == value.gain && old.pitch == value.pitch && old.loop == value.loop &&
                       old.spatial == value.spatial && old.minDistance == value.minDistance &&
                       old.maxDistance == value.maxDistance;
            };
            if (found != m_Sources.end() &&
                (found->second.clip != item.source.clip ||
                 (item.source.clipVersion.HasValue() &&
                  found->second.version != item.source.clipVersion.GetVersion()) ||
                 !sameSettings(found->second.settings))) {
                if (!audio.Stop(found->second.voice, error))
                    return false;
                m_Sources.erase(found);
                found = m_Sources.end();
            }
            if (found == m_Sources.end()) {
                AssetVersionRef<AudioClipAsset> clip = item.source.clipVersion;
                bool fallback = false;
                if (clip.HasValue() && clip.GetId() != item.source.clip) {
                    error = "Audio source pin identity mismatch";
                    return false;
                }
                if (!clip.HasValue() && !assets.Acquire(item.source.clip, clip, fallback, error)) {
                    if (!assets.Request(item.source.clip, error))
                        return false;
                    error.clear();
                    continue;
                }
                AudioVoiceHandle voice;
                if (!audio.Play(clip, item.source.settings, voice, error))
                    return false;
                m_Sources.emplace(item.id,
                                  Source{voice, item.source.clip, clip.GetVersion(), item.source.settings});
            } else if (audio.HasVoice(found->second.voice) &&
                       found->second.settings.position != item.source.settings.position) {
                if (!audio.SetPosition(found->second.voice, item.source.settings.position, error))
                    return false;
                found->second.settings.position = item.source.settings.position;
            }
        }
        for (auto it = m_Sources.begin(); it != m_Sources.end();) {
            if (live.contains(it->first)) {
                ++it;
                continue;
            }
            if (!audio.Stop(it->second.voice, error))
                return false;
            it = m_Sources.erase(it);
        }
        error.clear();
        return true;
    }
    void AudioExtractSystem::Stop(AudioService& audio) noexcept {
        for (size_t index = 0; index < m_RetiringCount;)
            if (!audio.HasVoice(m_Retiring[index].voice))
                m_Retiring[index] = m_Retiring[--m_RetiringCount];
            else
                ++index;
        for (const auto& [id, source] : m_Sources) {
            if (!audio.HasVoice(source.voice))
                continue;
            bool found = false;
            for (size_t index = 0; index < m_RetiringCount; ++index)
                found |= m_Retiring[index].voice == source.voice;
            if (!found) {
                // 同时 admitted voice 本就不超过256；上方剔除已完成句柄后不会丢失仍活着的所有权。
                if (m_RetiringCount == m_Retiring.size())
                    std::terminate();
                m_Retiring[m_RetiringCount++] = {source.voice, false};
            }
        }
        m_Sources.clear();
        m_Scene = {};
        m_World = 0;
        try {
            std::string ignored;
            (void)PumpRetiring(audio, ignored);
        } catch (...) {
        }
    }
    bool AudioExtractSystem::PumpRetiring(AudioService& audio, std::string& error) {
        for (size_t index = 0; index < m_RetiringCount;) {
            auto& value = m_Retiring[index];
            if (!audio.HasVoice(value.voice)) {
                value = m_Retiring[--m_RetiringCount];
                continue;
            }
            if (!value.queued) {
                if (!audio.Stop(value.voice, error))
                    return false;
                value.queued = true;
            }
            ++index;
        }
        error.clear();
        return true;
    }
    bool AudioExtractSystem::StopAndDrain(AudioService& audio, std::string& error) {
        if (m_World)
            m_StoppingWorld = m_World;
        Stop(audio);
        audio.Update(0);
        if (!PumpRetiring(audio, error))
            return false;
        if (m_RetiringCount) {
            error = "Busy: scene audio voices/stream reads are retiring; preserve package mount and retry";
            return false;
        }
        error.clear();
        return true;
    }
} // namespace Catalpa
