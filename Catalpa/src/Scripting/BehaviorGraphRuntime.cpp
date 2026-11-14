#include "catpch.h"

#include "Scripting/BehaviorGraphRuntime.h"

namespace Catalpa {
	void BehaviorGraphRuntime::Stop() noexcept {
		if (std::this_thread::get_id() != m_Owner)
			std::terminate();
		m_Instances.clear();
		m_AdmissionFailure.reset();
		m_Scene = 0;
	}

	bool BehaviorGraphRuntime::Enqueue(Instance& instance, std::string_view event, std::string& error) {
		for (const auto& node : instance.asset.Get()->nodes)
			if (((node.type == "Start" || node.type == "Update") && node.type == event) ||
			    (node.type == "Event" && node.event == event)) {
				if (instance.queue.size() >= 64) {
					error = "Behavior event queue limit exceeded";
					return false;
				}
				instance.queue.push_back({node.id, instance.seconds, std::string(event)});
				return true;
			}
		return true;
	}

	bool BehaviorGraphRuntime::Emit(PersistentEntityId entity, std::string event, std::string& error) {
		if (event.empty() || event.size() > 128 || event.find('\0') != std::string::npos || event == "Start" ||
		    event == "Update") {
			error = "Use a bounded authored event name";
			return false;
		}
		if (std::this_thread::get_id() != m_Owner) {
			error = "Behavior event uses another owner thread";
			return false;
		}
		const auto found = m_Instances.find(entity);
		if (found == m_Instances.end() || !found->second.asset.HasValue() || found->second.failed) {
			error = "Behavior instance is unavailable";
			return false;
		}
		return Enqueue(found->second, event, error);
	}

	bool BehaviorGraphRuntime::Prepare(Instance& candidate, EntityReference entity,
	    const BehaviorGraphComponent& author, AssetVersionRef<BehaviorGraphAsset> asset, std::string& error) {
		if (!ValidateBehaviorGraph(*asset.Get(), error) || !ValidateBehaviorVariables(author.variables, error))
			return false;

		candidate.entity = entity;
		candidate.author = author;
		candidate.asset = std::move(asset);
		candidate.debug.entity = entity.persistent;
		candidate.debug.graph = candidate.asset.GetId();
		candidate.debug.version = candidate.asset.GetVersion();
		candidate.debug.variables = candidate.asset.Get()->variables;
		for (const auto& [name, value] : author.variables) {
			const auto variable = candidate.debug.variables.find(name);
			if (variable == candidate.debug.variables.end()) {
				error = "Unknown behavior variable override: " + name;
				return false;
			}
			variable->second =
			    std::holds_alternative<double>(value) ? std::get<double>(value) : double(std::get<int64_t>(value));
		}
		return Enqueue(candidate, "Start", error);
	}

	/**
	 * @brief 执行实例队列中的节点并更新调试状态。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in,out] instance 持有本次执行所用变量和调试状态的行为图实例。
	 * @param[in] pending 指明从哪个待处理节点开始执行的工作项。
	 * @param[in,out] budget 本次调度剩余的节点执行次数，由各次执行共享。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool BehaviorGraphRuntime::Run(
	    Scene& scene, Instance& instance, Pending pending, uint32_t& budget, std::string& error) {
		auto variables = instance.debug.variables;
		std::vector<std::string> emitted;
		std::optional<Pending> continuation;
		ScriptApi api{scene, instance.entity, {}};
		auto current = pending.node;
		while (current.HasValue()) {
			if (!budget) {
				error = "Behavior instruction budget exceeded";
				return false;
			}
			--budget;
			const auto& nodes = instance.asset.Get()->nodes;
			const auto it =
			    std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == current; });
			if (it == nodes.end()) {
				error = "Behavior continuation node disappeared";
				return false;
			}
			const auto& node = *it;
			instance.debug.node = node.id;
			instance.debug.event = pending.event;
			std::string port = "next";
			if (node.type == "SetNumber")
				variables.at(node.variable) = node.number;
			else if (node.type == "AddNumber")
				variables.at(node.variable) += node.number;
			else if (node.type == "Condition")
				port = variables.at(node.variable) > node.number ? "true" : "false";
			else if (node.type == "Translate") {
				if (!api.Translate(node.vector, error))
					return false;
			} else if (node.type == "EmitEvent")
				emitted.push_back(node.event);
			else if (node.type == "Stop")
				break;
			const auto next = node.flow.find(port);
			current = next == node.flow.end() ? StableId{} : next->second;
			if (node.type == "Wait" && current.HasValue()) {
				continuation = Pending{current, instance.seconds + node.number, pending.event};
				break;
			}
		}
		if (std::any_of(
		        variables.begin(), variables.end(), [](const auto& value) { return !std::isfinite(value.second); }) ||
		    (continuation && !std::isfinite(continuation->wake))) {
			error = "Behavior numeric or event budget exceeded";
			return false;
		}
		auto queue = instance.queue;
		if (continuation)
			queue.push_back(*continuation);
		/* 先预留所有继续点，写世界成功后再一起发布变量和消息。 */
		for (const auto& event : emitted)
			for (const auto& node : instance.asset.Get()->nodes)
				if (node.type == "Event" && node.event == event)
					queue.push_back({node.id, instance.seconds, event});
		if (queue.size() > 64) {
			error = "Behavior event queue limit exceeded";
			return false;
		}
		if (!api.commands.Commit(scene, error))
			return false;
		instance.queue.swap(queue);
		instance.debug.variables.swap(variables);
		return true;
	}

	void BehaviorGraphRuntime::Tick(Scene& scene, AssetManager& assets, double seconds) {
		m_AdmissionFailure.reset();
		if (std::this_thread::get_id() != m_Owner)
			std::terminate();
		if (!std::isfinite(seconds) || seconds < 0)
			return;
		if (m_Scene != scene.GetWorld().GetIdentity()) {
			Stop();
			m_Scene = scene.GetWorld().GetIdentity();
		}
		std::map<PersistentEntityId, std::pair<EntityReference, BehaviorGraphComponent>> components;
		const auto status = scene.GetWorld().ForEach([&](EntityId entity) -> WorldStatus {
			if (!scene.GetWorld().Has<BehaviorGraphComponent>(entity))
				return {};
			BehaviorGraphComponent component;
			const auto result = scene.GetWorld().Get(entity, component);
			if (result.IsSuccess() && component.enabled) {
				const auto ref = scene.MakeReference(entity);
				components.emplace(ref.persistent, std::make_pair(ref, std::move(component)));
			}
			return result;
		});
		if (!status.IsSuccess())
			return;
		std::erase_if(m_Instances, [&](const auto& p) {
			const auto found = components.find(p.first);
			return found == components.end() || found->second.first != p.second.entity;
		});
		for (const auto& [id, author] : components) {
			AssetVersionRef<BehaviorGraphAsset> asset;
			bool fallback = false;
			std::string error;
			if (!assets.Acquire(author.second.graph, asset, fallback, error) || fallback) {
				(void)assets.Request(author.second.graph, error);
				continue;
			}
			if (!m_Instances.contains(id) && m_Instances.size() >= 256) {
				if (!m_AdmissionFailure)
					m_AdmissionFailure =
					    BehaviorDebugSnapshot{id, {}, {}, "Behavior instance limit (256) exceeded", {}};
				continue;
			}
			auto& instance = m_Instances[id];
			const bool changed = !instance.asset.HasValue() || instance.author != author.second ||
			    instance.asset.GetVersion() != asset.GetVersion();
			if (!changed) {
				instance.rejectedVersion = 0;
				instance.rejectionError.clear();
			}
			if (changed &&
			    (instance.rejectedVersion != asset.GetVersion() || instance.rejectedAuthor != author.second)) {
				Instance candidate;
				if (Prepare(candidate, author.first, author.second, asset, error))
					instance = std::move(candidate);
				else {
					/* 先完整验证候选，再替换正在运行的版本；拒绝的草稿不能清掉旧变量/Wait。 */
					instance.rejectedVersion = asset.GetVersion();
					instance.rejectedAuthor = author.second;
					instance.rejectionError = error;
					if (!instance.asset.HasValue()) {
						instance.entity = author.first;
						instance.debug.entity = id;
						instance.debug.graph = author.second.graph;
					}
				}
			}
			if (instance.failed || !instance.asset.HasValue())
				continue;
			instance.seconds += seconds;
			if (!std::isfinite(instance.seconds) || !Enqueue(instance, "Update", error)) {
				instance.failed = true;
				instance.debug.error = error.empty() ? "Behavior clock overflow" : error;
				continue;
			}
			uint32_t budget = 1024;
			/* 等待中的继续点不阻塞其它已经就绪的事件。 */
			size_t inspected = 0;
			while (!instance.queue.empty() && inspected < instance.queue.size()) {
				auto pending = std::move(instance.queue.front());
				instance.queue.pop_front();
				if (pending.wake > instance.seconds) {
					instance.queue.push_back(std::move(pending));
					++inspected;
					continue;
				}
				inspected = 0;
				if (!Run(scene, instance, std::move(pending), budget, error)) {
					instance.failed = true;
					instance.debug.error = std::move(error);
					instance.queue.clear();
					break;
				}
			}
		}
	}

	std::vector<BehaviorDebugSnapshot> BehaviorGraphRuntime::Snapshot(bool includeVariables) const {
		if (std::this_thread::get_id() != m_Owner)
			return {};
		std::vector<BehaviorDebugSnapshot> result;
		for (const auto& [id, instance] : m_Instances) {
			const auto& value = instance.debug;
			BehaviorDebugSnapshot snapshot{
			    value.entity, value.node, value.event, value.error, {}, value.graph, value.version};
			if (snapshot.error.empty())
				snapshot.error = instance.rejectionError;
			if (includeVariables)
				snapshot.variables = value.variables;
			result.push_back(std::move(snapshot));
		}
		if (m_AdmissionFailure)
			result.push_back(*m_AdmissionFailure);
		return result;
	}
}
