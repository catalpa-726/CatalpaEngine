#include "catpch.h"

#include "Runtime/Systems/ParticleAuthoring.h"

#include "Scene/Components/ParticleEmitterComponent.h"
#include "Scene/SceneDynamicComponents.h"

namespace Catalpa {
	namespace {
		const ArchiveValue& Field(const ArchiveValue& value, const char* name) {
			const auto* field = value.Find(name);
			if (!field)
				throw std::invalid_argument(std::string("Missing particle emitter field: ") + name);
			return *field;
		}

		ParticleEmitterComponent Read(const ArchiveValue& value) {
			const auto* object = std::get_if<ArchiveValue::Object>(&value.data);
			if (!object)
				throw std::invalid_argument("Particle emitter must be an object");
			for (const auto& [key, item] : *object)
				if (key != "schemaVersion" && key != "effect" && key != "enabled" && key != "emitting" &&
				    key != "seedOverride" && key != "restartRevision")
					throw std::invalid_argument("Unknown particle emitter field: " + key);
			uint64_t version = 0, seed = 0, revision = 0;
			if (!Field(value, "schemaVersion").ReadUnsigned(version) || version != 1 ||
			    !Field(value, "seedOverride").ReadUnsigned(seed) || seed > UINT32_MAX ||
			    !Field(value, "restartRevision").ReadUnsigned(revision) || !revision)
				throw std::invalid_argument("Particle emitter version/seed/restart is invalid");
			ParticleEmitterComponent result;
			const auto* identity = std::get_if<std::string>(&Field(value, "effect").data);
			const auto* enabled = std::get_if<bool>(&Field(value, "enabled").data);
			const auto* emitting = std::get_if<bool>(&Field(value, "emitting").data);
			if (!identity || !AssetId::TryParse(*identity, result.effect) || !enabled || !emitting)
				throw std::invalid_argument("Particle emitter identity/bool field is invalid");
			result.enabled = *enabled;
			result.emitting = *emitting;
			result.seedOverride = uint32_t(seed);
			result.restartRevision = revision;
			return result;
		}

		WorldStatus Apply(World& world, EntityId entity, const std::optional<ParticleEmitterComponent>& value) {
			if (value)
				return world.Has<ParticleEmitterComponent>(entity) ? world.Set(entity, *value)
				                                                   : world.Add(entity, *value);
			return world.Has<ParticleEmitterComponent>(entity) ? world.Remove<ParticleEmitterComponent>(entity)
			                                                   : WorldStatus{};
		}
	} /* 命名空间 */
	bool CaptureParticleComponents(const World& world, EntityId entity,
	    std::map<std::string, ArchiveValue, std::less<>>& fields, std::string& error) try {
		if (world.Has<ParticleEmitterComponent>(entity)) {
			ParticleEmitterComponent value;
			const auto status = world.Get(entity, value);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			fields["catalpa.particle_emitter"] = ArchiveValue{ArchiveValue::Object{
			    {"schemaVersion", ArchiveValue{uint64_t(1)}}, {"effect", ArchiveValue{value.effect.ToString()}},
			    {"enabled", ArchiveValue{value.enabled}}, {"emitting", ArchiveValue{value.emitting}},
			    {"seedOverride", ArchiveValue{uint64_t(value.seedOverride)}},
			    {"restartRevision", ArchiveValue{value.restartRevision}}}};
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool ValidateParticleComponent(std::string_view type, const ArchiveValue& value, std::string& error) try {
		if (type != "catalpa.particle_emitter") {
			error = "Unknown particle component type";
			return false;
		}
		(void)Read(value);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	std::vector<AssetId> ParticleComponentDependencies(std::string_view type, const ArchiveValue& value) {
		return type == "catalpa.particle_emitter" ? std::vector<AssetId>{Read(value).effect} : std::vector<AssetId>{};
	}

	bool BindParticleComponentVersions(
	    World& world, EntityId entity, std::span<const AssetDependencyValue> dependencies, std::string& error) try {
		if (!world.Has<ParticleEmitterComponent>(entity)) {
			error.clear();
			return true;
		}
		ParticleEmitterComponent value;
		auto status = world.Get(entity, value);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		bool found = false;
		for (const auto& dependency : dependencies)
			if (dependency.id == value.effect && dependency.cppType == typeid(ParticleEffect)) {
				found = AssetVersionRef<ParticleEffect>::TryCreate(dependency.id, dependency.version,
				    std::static_pointer_cast<const ParticleEffect>(dependency.value), value.effectVersion, error);
				break;
			}
		if (!found) {
			error = "Scene particle emitter has no matching fixed effect dependency";
			return false;
		}
		status = world.Set(entity, value);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool RestoreParticleComponents(World& world, EntityId entity,
	    const std::map<std::string, ArchiveValue, std::less<>>& fields, std::string& error) try {
		std::optional<ParticleEmitterComponent> value;
		if (const auto found = fields.find("catalpa.particle_emitter"); found != fields.end())
			value = Read(found->second);
		const auto status = Apply(world, entity, value);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool RestoreParticleComponents(Scene& scene, EntityId entity,
	    const std::map<std::string, ArchiveValue, std::less<>>& fields, std::string& error) try {
		std::optional<ParticleEmitterComponent> value;
		if (const auto found = fields.find("catalpa.particle_emitter"); found != fields.end())
			value = Read(found->second);
		const auto status =
		    QueueDynamicComponent(scene, entity, [entity, value](World& world) { return Apply(world, entity, value); });
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool CollectParticleComponentVersions(
	    const World& world, EntityId entity, std::vector<AssetDependencyValue>& output, std::string& error) try {
		if (!world.Has<ParticleEmitterComponent>(entity)) {
			error.clear();
			return true;
		}
		ParticleEmitterComponent component;
		const auto status = world.Get(entity, component);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		const auto& fixed = component.effectVersion;
		if (fixed.HasValue()) {
			for (const auto& value : output)
				if (value.id == fixed.GetId()) {
					if (value.version != fixed.GetVersion() || value.cppType != typeid(ParticleEffect)) {
						error = "Scene contains incompatible fixed versions of one particle identity";
						return false;
					}
					error.clear();
					return true;
				}
			output.push_back({fixed.GetId(), fixed.GetVersion(), fixed.GetShared(), typeid(ParticleEffect)});
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa */
