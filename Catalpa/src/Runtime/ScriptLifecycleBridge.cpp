#include "catpch.h"

#include "EngineRuntime.h"
#include "Scene/SceneSerializer.h"
#include "Scene/Components/ScriptComponent.h"
#include "Log/Log.h"
namespace Catalpa {
	bool EngineRuntime::PollScriptSceneSwitch(SceneHandle& output, std::string& error) {
		output = {};
		error.clear();
		if (!m_PendingScriptScene)
			return false;
		const auto request = *m_PendingScriptScene;
		const auto* scene = m_Scenes.GetActive();
		if (!scene || scene->MakeReference(scene->Resolve(request.requester.persistent)) != request.requester ||
		    !m_Scripts.IsCurrentSceneRequest(request)) {
			m_PendingScriptScene.reset();
			return false;
		}
		const auto state = m_Assets->GetStatus(request.scene);
		if (state == AssetStatus::Unloaded) {
			if (!m_Assets->Request(request.scene, error))
				m_PendingScriptScene.reset();
			return false;
		}
		if (state == AssetStatus::Failed || state == AssetStatus::Cancelled) {
			error = m_Assets->GetDiagnostic(request.scene);
			m_PendingScriptScene.reset();
			return false;
		}
		if (state != AssetStatus::Ready)
			return false;
		AssetVersionRef<SceneAsset> asset;
		bool fallback = false;
		if (!m_Assets->Acquire(request.scene, asset, fallback, error) || fallback) {
			m_PendingScriptScene.reset();
			return false;
		}
		SceneHandle candidate;
		if (!SceneSerializer::Instantiate(*asset.Get(), m_Scenes, m_Assets.get(), candidate, error)) {
			m_PendingScriptScene.reset();
			return false;
		}
		/* Instantiate不改变active；旧VM还活着，最后再核对异步请求的代次。 */
		if (!m_Scripts.IsCurrentSceneRequest(request)) {
			(void)m_Scenes.Unload(candidate);
			m_PendingScriptScene.reset();
			return false;
		}
		m_PendingScriptScene.reset();
		output = candidate;
		return true;
	}

	/**
	 * @brief 将动态系统事件传给场景脚本，以十进制文本保留 64 位序号。
	 *
	 * @param[out] scene 接收动态事件的场景。
	 */
	void EngineRuntime::ForwardDynamicEvents(Scene& scene) {
		/* 64 位事件序号以十进制文本传入 Lua，避免 double 超过 2^53 后丢失身份。 */
		const auto emit = [&](EntityReference target, std::string_view channel, const ScriptValues& values) {
			if (!target.HasValue() || target.world != scene.GetWorld().GetIdentity() ||
			    !scene.GetWorld().IsAlive(target.resolved) || !scene.GetWorld().Has<ScriptComponent>(target.resolved))
				return;
			std::string error;
			if (!m_Scripts.EmitEvent(target, channel, values, error) && !error.empty())
				CAT_CORE_WARN("Script event: {0}", error);
		};
		for (const auto& value : m_Animation.TakeEvents())
			emit(value.entity, "animation",
			    {{"name", value.event.name}, {"payload", value.event.payload}, {"time", value.event.time}});
		std::vector<PhysicsContactEvent> contacts;
		m_Physics.TakeEvents(contacts);
		for (const auto& contact : contacts) {
			ScriptValues fields{{"kind", int64_t(contact.kind)}, {"trigger", contact.trigger}, {"point", contact.point},
			    {"normal", contact.normal}, {"tick", std::to_string(contact.tick)}};
			fields["other"] = contact.second.persistent;
			emit(contact.first, contact.trigger ? "trigger" : "collision", fields);
			fields["other"] = contact.first.persistent;
			fields["normal"] = -contact.normal;
			emit(contact.second, contact.trigger ? "trigger" : "collision", fields);
		}
		std::vector<EntityReference> listeners;
		(void)scene.GetWorld().ForEach([&](EntityId id) {
			if (scene.GetWorld().Has<ScriptComponent>(id))
				listeners.push_back(scene.MakeReference(id));
			return WorldStatus{};
		});
		std::sort(listeners.begin(), listeners.end(),
		    [](const auto& a, const auto& b) { return a.persistent < b.persistent; });
		for (const auto& event : m_GameUISystem.TakeEvents()) {
			ScriptValues fields{{"element", event.element}, {"type", event.type}, {"value", event.value},
			    {"source", event.source}, {"sequence", std::to_string(event.sequence)}};
			for (const auto& listener : listeners)
				emit(listener, "ui", fields);
		}
		for (const auto& event : m_Audio.TakeAcknowledgements()) {
			ScriptValues fields{{"voice", std::to_string(event.handle.value)}, {"success", event.success},
			    {"error", event.error}, {"sequence", std::to_string(event.sequence)}};
			for (const auto& listener : listeners)
				emit(listener, "audio", fields);
		}
		for (const auto& request : m_Scripts.TakeSceneRequests())
			if (m_Scripts.IsCurrentSceneRequest(request))
				m_PendingScriptScene = request; /* 同tick最后一个有效意图确定胜出。 */
	}
} /* 命名空间 Catalpa */
