#include "catpch.h"

#include "Scene/DomainComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneSubtree.h"

#include "Lighting/LightValidation.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
	const ComponentType& ComponentTraits<LightComponent>::Get() {
		return Detail::MakeComponentType<LightComponent>();
	}

	const ComponentType& ComponentTraits<EnvironmentComponent>::Get() {
		return Detail::MakeComponentType<EnvironmentComponent>();
	}

	WorldStatus Scene::QueueSetLight(EntityId id, const LightComponent& light) try {
		std::string error;
		if (!ValidateLightParameters(light.parameters, error)) {
			m_ComponentDiagnostic = std::move(error);
			return {WorldError::InvalidArgument, "Invalid light parameters"};
		}
		if (m_ComponentSchemas) {
			const auto& p = light.parameters;
			ArchiveValue fields{ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
			    {"type", ArchiveValue{uint64_t(p.type)}},
			    {"color",
			        ArchiveValue{ArchiveValue::Array{ArchiveValue{double(p.color.x)}, ArchiveValue{double(p.color.y)},
			            ArchiveValue{double(p.color.z)}}}},
			    {"intensity", ArchiveValue{double(p.intensity)}}, {"range", ArchiveValue{double(p.range)}},
			    {"inner_cone", ArchiveValue{double(p.innerConeRadians)}},
			    {"outer_cone", ArchiveValue{double(p.outerConeRadians)}}, {"cast_shadows", ArchiveValue{p.castShadows}},
			    {"shadow_bias", ArchiveValue{double(p.shadowBiasMeters)}}, {"enabled", ArchiveValue{light.enabled}}}};
			if (auto status = ValidateComponent("catalpa.light", fields); !status.IsSuccess())
				return status;
		}
		return m_Commands.Enqueue([id, light](World& world) {
			return world.Has<LightComponent>(id) ? world.Set(id, light) : world.Add(id, light);
		});
	}

	catch (const std::bad_alloc&) {

		return {WorldError::AllocationFailed, "Cannot capture light edit"};
	}

	WorldStatus Scene::QueueSetEnvironment(EntityId id, const EnvironmentComponent& environment) try {
		const auto& settings = environment.settings;
		if (!environment.environment.HasValue() || !ValidateEnvironmentSettings(settings, m_ComponentDiagnostic))
			return {WorldError::InvalidArgument, "Invalid environment reference or settings"};
		if (m_ComponentSchemas) {
			ArchiveValue fields{ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
			    {"environment", ArchiveValue{environment.environment.id.ToString()}},
			    {"intensity", ArchiveValue{double(settings.intensity)}},
			    {"rotation", ArchiveValue{double(settings.rotationRadians)}},
			    {"exposure_ev", ArchiveValue{double(settings.exposureEV)}},
			    {"draw_background", ArchiveValue{settings.drawBackground}},
			    {"fog_enabled", ArchiveValue{settings.fogEnabled}},
			    {"fog_color",
			        ArchiveValue{ArchiveValue::Array{ArchiveValue{double(settings.fogColor.x)},
			            ArchiveValue{double(settings.fogColor.y)}, ArchiveValue{double(settings.fogColor.z)}}}},
			    {"fog_density", ArchiveValue{double(settings.fogDensityPerMeter)}},
			    {"enabled", ArchiveValue{environment.enabled}}}};
			if (auto status = ValidateComponent("catalpa.environment", fields); !status.IsSuccess())
				return status;
		}
		return m_Commands.Enqueue([id, environment](World& world) {
			return world.Has<EnvironmentComponent>(id) ? world.Set(id, environment) : world.Add(id, environment);
		});
	}

	catch (const std::bad_alloc&) {

		return {WorldError::AllocationFailed, "Cannot capture environment edit"};
	}

	WorldStatus Scene::QueueRemoveLight(EntityId id) {
		return m_Commands.Enqueue([id](World& world) { return world.Remove<LightComponent>(id); });
	}

	WorldStatus Scene::QueueRemoveEnvironment(EntityId id) {
		return m_Commands.Enqueue([id](World& world) { return world.Remove<EnvironmentComponent>(id); });
	}

	bool SceneSubtreeSnapshot::Node::SameAuthorData(const Node& b) const {
		if (identity.persistent != b.identity.persistent || identity.name != b.identity.name || parent != b.parent ||
		    local.translation != b.local.translation || local.rotation != b.local.rotation ||
		    local.scale != b.local.scale || camera.has_value() != b.camera.has_value() ||
		    mesh.has_value() != b.mesh.has_value() || light.has_value() != b.light.has_value() ||
		    environment.has_value() != b.environment.has_value() || script != b.script || behavior != b.behavior ||
		    prefab != b.prefab || !SameDomainComponents(dynamicComponents, b.dynamicComponents) ||
		    !std::ranges::equal(dynamicDependencies, b.dynamicDependencies, [](const auto& a, const auto& c) {
			    return a.id == c.id && a.version == c.version && a.cppType == c.cppType &&
			        a.value.get() == c.value.get();
		    }))
			return false;
		if (camera &&
		    (camera->lens != b.camera->lens || camera->enabled != b.camera->enabled ||
		        camera->priority != b.camera->priority || camera->viewId != b.camera->viewId ||
		        camera->historyRevision != b.camera->historyRevision))
			return false;
		if (mesh) {
			const auto sameVersion = [](const auto& a, const auto& b) {
				return a.GetId() == b.GetId() && a.GetVersion() == b.GetVersion() && a.Get() == b.Get();
			};
			if (mesh->mesh != b.mesh->mesh || mesh->visible != b.mesh->visible || mesh->color != b.mesh->color ||
			    !sameVersion(mesh->meshVersion, b.mesh->meshVersion) ||
			    !std::ranges::equal(mesh->materials, b.mesh->materials, sameVersion) ||
			    !std::ranges::equal(mesh->lods, b.mesh->lods, sameVersion) || mesh->lodErrors != b.mesh->lodErrors)
				return false;
		}
		if (light) {
			const auto& a = light->parameters;
			const auto& p = b.light->parameters;
			if (light->enabled != b.light->enabled || a.type != p.type || a.color != p.color ||
			    a.intensity != p.intensity || a.range != p.range || a.innerConeRadians != p.innerConeRadians ||
			    a.outerConeRadians != p.outerConeRadians || a.castShadows != p.castShadows ||
			    a.shadowBiasMeters != p.shadowBiasMeters)
				return false;
		}
		if (environment) {
			const auto& a = environment->settings;
			const auto& e = b.environment->settings;
			if (environment->enabled != b.environment->enabled ||
			    environment->environment != b.environment->environment || a.intensity != e.intensity ||
			    a.rotationRadians != e.rotationRadians || a.exposureEV != e.exposureEV ||
			    a.drawBackground != e.drawBackground || a.fogEnabled != e.fogEnabled || a.fogColor != e.fogColor ||
			    a.fogDensityPerMeter != e.fogDensityPerMeter)
				return false;
		}
		return true;
	}

	WorldStatus Scene::CaptureForUndo(EntityId root, SceneSubtreeSnapshot& output) const try {
		if (!CanEditNow())
			return {WorldError::Busy, "Scene is not at an edit boundary"};
		if (auto status = ValidateEntity(root); !status.IsSuccess())
			return status;
		/* 先构建子节点索引，捕获过程只遍历指定根的后代。 */
		std::map<uint32_t, std::vector<EntityId>> children;
		auto scan = m_World.ForEach([&](EntityId id) {
			RelationshipComponent relation;
			const auto status = GetRelationship(id, relation);
			if (status.IsSuccess())
				children[relation.parent.index].push_back(id);
			return status;
		});
		if (!scan.IsSuccess())
			return scan;

		SceneSubtreeSnapshot snapshot;
		snapshot.m_World = m_World.GetIdentity();
		std::vector<EntityId> order{root};
		std::set<uint32_t> seen;
		for (size_t i = 0; i < order.size(); ++i) {
			const auto id = order[i];
			if (!seen.insert(id.index).second)
				return {WorldError::InvalidArgument, "Cyclic scene subtree"};
			auto& node = snapshot.m_Nodes.emplace_back();
			TransformComponent transform;
			RelationshipComponent relation;
			if (auto s = GetIdentity(id, node.identity); !s.IsSuccess())
				return s;
			if (auto s = GetTransform(id, transform); !s.IsSuccess())
				return s;
			if (auto s = GetRelationship(id, relation); !s.IsSuccess())
				return s;
			node.local = transform.local;
			if (i == 0)
				snapshot.m_ParentGeneration = relation.parent;
			if (relation.parent.HasValue()) {
				IdentityComponent parent;
				if (auto s = GetIdentity(relation.parent, parent); !s.IsSuccess())
					return s;
				node.parent = parent.persistent;
			}
			size_t captured = 3;
			auto capture = [&]<class T>(std::optional<T>& dest) -> WorldStatus {
				if (!m_World.Has<T>(id))
					return {};
				++captured;
				return m_World.Get(id, dest.emplace());
			};
			if (auto s = capture(node.camera); !s.IsSuccess())
				return s;
			if (auto s = capture(node.mesh); !s.IsSuccess())
				return s;
			if (auto s = capture(node.light); !s.IsSuccess())
				return s;
			if (auto s = capture(node.environment); !s.IsSuccess())
				return s;
			if (auto s = capture(node.script); !s.IsSuccess())
				return s;
			if (auto s = capture(node.behavior); !s.IsSuccess())
				return s;
			if (auto s = capture(node.prefab); !s.IsSuccess())
				return s;
			std::string domainError;
			if (!CaptureDomainComponents(*this, id, node.dynamicComponents, domainError))
				return {WorldError::InvalidArgument, "Cannot capture domain components"};
			if (!CollectDomainComponentVersions(m_World, id, node.dynamicDependencies, domainError))
				return {WorldError::InvalidArgument, "Cannot capture fixed domain versions"};
			captured += node.dynamicComponents.size();
			size_t count = 0;
			if (auto s = m_World.CountComponents(id, count); !s.IsSuccess())
				return s;
			if (count != captured)
				return {WorldError::InvalidArgument, "Subtree contains a component without undo capture"};
			if (auto it = children.find(id.index); it != children.end())
				order.insert(order.end(), it->second.begin(), it->second.end());
		}
		output = std::move(snapshot);
		return {};
	} catch (const std::bad_alloc&) {
		return {WorldError::AllocationFailed, "Cannot snapshot subtree"};
	}

	WorldStatus Scene::RemoveForUndo(
	    EntityId root, SceneSubtreeSnapshot& output, const SceneSubtreeSnapshot* expected) try {
		SceneSubtreeSnapshot snapshot;
		if (auto status = CaptureForUndo(root, snapshot); !status.IsSuccess())
			return status;
		if (expected) {
			if (expected->m_World != snapshot.m_World || expected->m_Nodes.size() != snapshot.m_Nodes.size())
				return {WorldError::Busy, "Subtree changed outside this command"};
			std::map<PersistentEntityId, const SceneSubtreeSnapshot::Node*> previous;
			for (const auto& node : expected->m_Nodes)
				previous.emplace(node.identity.persistent, &node);
			for (const auto& node : snapshot.m_Nodes) {
				const auto it = previous.find(node.identity.persistent);
				if (it == previous.end() || !node.SameAuthorData(*it->second))
					return {WorldError::Busy, "Subtree author data changed"};
			}
		}
		if (auto status = DestroySubtree(root); !status.IsSuccess())
			return status;
		output = std::move(snapshot);
		return {};
	} catch (const std::bad_alloc&) {
		return {WorldError::AllocationFailed, "Cannot compare subtree snapshot"};
	}

	WorldStatus Scene::RestoreForUndo(const SceneSubtreeSnapshot& snapshot, EntityId reboundParent) try {
		if (!CanEditNow())
			return {WorldError::Busy, "Scene is not at an edit boundary"};
		if (snapshot.m_World != m_World.GetIdentity() || snapshot.m_Nodes.empty())
			return {WorldError::ForeignWorld, "Subtree belongs to another scene"};
		const auto expectedParent = reboundParent.HasValue() ? reboundParent : snapshot.m_ParentGeneration;
		if (Resolve(snapshot.m_Nodes.front().parent) != expectedParent)
			return {WorldError::InvalidEntity, "Subtree parent was removed or replaced"};
		for (const auto& node : snapshot.m_Nodes) {
			if (Resolve(node.identity.persistent).HasValue())
				return {WorldError::AlreadyExists, "Subtree identity was reused"};
		}
		std::vector<EntityId> created;
		created.reserve(snapshot.m_Nodes.size());
		const bool dirty = m_Dirty;
		/* 仅回滚本次恢复创建的节点，逆序移除后重置场景的修改标记。 */
		auto rollback = [&](WorldStatus failure) {
			for (size_t i = created.size(); i-- > 0;) {
				m_Entities.erase(snapshot.m_Nodes[i].identity.persistent);
				if (auto s = m_World.Destroy(created[i]); !s.IsSuccess())
					return s;
			}
			m_Dirty = dirty;
			return failure;
		};
		try {
			for (const auto& node : snapshot.m_Nodes) {
				const auto parent = node.parent.HasValue() ? Resolve(node.parent) : EntityId{};
				if (node.parent.HasValue() && !parent.HasValue())
					return rollback({WorldError::InvalidEntity, "Parent no longer exists"});
				EntityId id;
				if (auto s = m_World.Create(id); !s.IsSuccess())
					return rollback(s);
				created.push_back(id);
				if (auto s = InitializeEntity(id, node.identity.persistent, node.identity.name, node.local, parent);
				    !s.IsSuccess())
					return rollback(s);
				if (node.camera)
					if (auto s = m_World.Add(id, *node.camera); !s.IsSuccess())
						return rollback(s);
				if (node.mesh)
					if (auto s = m_World.Add(id, *node.mesh); !s.IsSuccess())
						return rollback(s);
				if (node.light)
					if (auto s = m_World.Add(id, *node.light); !s.IsSuccess())
						return rollback(s);
				if (node.environment)
					if (auto s = m_World.Add(id, *node.environment); !s.IsSuccess())
						return rollback(s);
				if (node.script)
					if (auto s = m_World.Add(id, *node.script); !s.IsSuccess())
						return rollback(s);
				if (node.behavior)
					if (auto s = m_World.Add(id, *node.behavior); !s.IsSuccess())
						return rollback(s);
				std::string domainError;
				if (!RestoreDomainComponents(m_World, id, node.dynamicComponents, domainError) ||
				    !BindDomainComponentVersions(m_World, id, node.dynamicDependencies, domainError))
					return rollback({WorldError::CommandFailed, "Cannot restore domain components"});
				if (node.prefab)
					if (auto s = m_World.Add(id, *node.prefab); !s.IsSuccess())
						return rollback(s);
			}
		} catch (...) {
			return rollback({WorldError::CommandFailed, "Subtree restoration failed"});
		}
		return {};
	} catch (const std::bad_alloc&) {

		return {WorldError::AllocationFailed, "Cannot restore subtree"};
	}
}
