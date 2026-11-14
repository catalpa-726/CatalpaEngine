#include "catpch.h"

#include "Runtime/Systems/ParticleSystem.h"

#include "Scene/Scene.h"
#include "Scene/Components/ParticleEmitterComponent.h"
#include "Assets/AssetManager.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
	const ComponentType& ComponentTraits<ParticleEmitterComponent>::Get() {
		return Detail::MakeComponentType<ParticleEmitterComponent>();
	}

	bool ParticleSystem::Tick(Scene& scene, AssetManager& assets, double dt, std::string& error) try {
		if (!std::isfinite(dt) || dt < 0 || dt > .25) {
			error = "Particle system dt outside [0,.25]";
			return false;
		}
		std::vector<std::pair<EntityId, ParticleEmitterComponent>> emitters;
		const auto status = scene.GetWorld().ForEach([&](EntityId entity) {
			if (scene.GetWorld().Has<ParticleEmitterComponent>(entity)) {
				ParticleEmitterComponent component;
				const auto result = scene.GetWorld().Get(entity, component);
				if (!result.IsSuccess())
					return result;
				emitters.emplace_back(entity, std::move(component));
			}
			return WorldStatus{};
		});
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		if (emitters.size() > 128) {
			error = "Particle emitter instance budget exceeded";
			return false;
		}
		const auto world = scene.GetWorld().GetIdentity();
		std::set<std::tuple<uint64_t, uint32_t, uint32_t>> alive;
		for (const auto& [entity, component] : emitters)
			if (component.enabled)
				alive.emplace(world, entity.index, entity.generation);
		std::erase_if(m_Instances,
		    [&](const auto& item) { return std::get<0>(item.first) == world && !alive.contains(item.first); });
		struct PreparedEmitter {
			EntityId entity;
			ParticleEmitterComponent component;
			AssetVersionRef<ParticleEffect> effect;
		};
		std::vector<PreparedEmitter> prepared;
		size_t capacities = 0;
		const auto admitCapacity = [&](size_t count) {
			if (!count || count > MaximumParticleCapacity || count > MaximumParticleCapacity - capacities) {
				error = "Combined particle pool capacity exceeds the Renderer budget of 16384 particles per "
				        "Scene";
				return false;
			}
			capacities += count;
			return true;
		};
		/* 先统计本 tick 最终采用的全部版本，再修改实例；扩容与缩容不应依赖实体遍历顺序。 */
		for (const auto& [entity, component] : emitters) {
			const auto key = std::make_tuple(world, entity.index, entity.generation);
			if (!component.enabled)
				continue;
			if (!component.effect.HasValue() || !component.restartRevision) {
				error = "Particle emitter identity/restart revision invalid";
				return false;
			}
			AssetVersionRef<ParticleEffect> fixed = component.effectVersion;
			if (fixed.HasValue() && fixed.GetId() != component.effect)
				fixed = {};
			if (!fixed.HasValue()) {
				const auto state = assets.GetStatus(component.effect);
				if (state == AssetStatus::Unloaded) {
					if (!assets.Request(component.effect, error))
						return false;
				}
				if (state == AssetStatus::Failed || state == AssetStatus::Cancelled) {
					error = assets.GetDiagnostic(component.effect);
					return false;
				}
				if (state != AssetStatus::Ready) {
					const auto prior = m_Instances.find(key);
					if (prior != m_Instances.end() && prior->second.effect.HasValue() &&
					    !admitCapacity(prior->second.effect.Get()->maxParticles))
						return false;
					continue;
				}
				bool fallback = false;
				if (!assets.Acquire(component.effect, fixed, fallback, error) || fallback) {
					error = "Particle emitter requires an exact effect version";
					return false;
				}
			}
			if (!admitCapacity(fixed.Get()->maxParticles))
				return false;
			if (!fixed.Get()->materialVersion.HasValue()) {
				error = "Runtime particle effect requires an exact transparent material dependency";
				return false;
			}
			prepared.push_back({entity, component, std::move(fixed)});
		}
		for (const auto& [entity, component, fixed] : prepared) {
			const auto key = std::make_tuple(world, entity.index, entity.generation);
			auto& instance = m_Instances[key];
			const uint32_t seed = component.seedOverride ? component.seedOverride : fixed.Get()->seed;
			if (!instance.effect.HasValue() || instance.effect.GetId() != fixed.GetId() ||
			    instance.effect.GetVersion() != fixed.GetVersion() || instance.restart != component.restartRevision ||
			    instance.seed != seed) {
				if (!instance.pool.Reset(*fixed.Get(), seed, error))
					return false;
				instance.effect = fixed;
				instance.restart = component.restartRevision;
				instance.seed = seed;
			}
			TransformComponent transform;
			const auto fetched = scene.GetTransform(entity, transform);
			if (!fetched.IsSuccess() || !transform.worldValid) {
				error = "Particle emitter requires an updated world transform";
				return false;
			}
			instance.entity = scene.MakeReference(entity);
			instance.emitter = transform.worldMatrix;
			instance.visible = true;
			if (!instance.pool.Tick(dt, component.emitting, instance.emitter, error))
				return false;
		}
		std::erase_if(m_Instances,
		    [&](const auto& item) { return std::get<0>(item.first) == world && !alive.contains(item.first); });
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	std::vector<ParticleEmitterSnapshot> ParticleSystem::Snapshot(uint64_t world) const {
		std::vector<ParticleEmitterSnapshot> snapshots;
		for (const auto& [key, instance] : m_Instances)
			if (std::get<0>(key) == world && instance.visible)
				snapshots.push_back({instance.entity, instance.effect.Get()->materialVersion,
				    instance.pool.Snapshot(instance.emitter), instance.pool.DroppedCount()});
		return snapshots;
	}

	void ParticleSystem::ReleaseWorld(uint64_t world) noexcept {
		std::erase_if(m_Instances, [&](const auto& item) { return std::get<0>(item.first) == world; });
	}
} /* 命名空间 Catalpa */
