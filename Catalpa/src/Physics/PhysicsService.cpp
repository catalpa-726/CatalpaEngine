#include "catpch.h"

#include "Physics/PhysicsService.h"

#include "Assets/AssetManager.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/Scene.h"
#include "Core/Math/TransformMath.h"
#include "Core/Serialization/AuthorJson.h"

namespace Catalpa {
	struct PhysicsService::Impl {
		std::unique_ptr<PhysicsWorld> world;
		PhysicsWorldConfig config;
		uint64_t scene = 0, sequence = 0, dropped = 0;
		struct BodyLink {
			PhysicsBodyHandle handle;
			AssetId shapeId, materialId;
			uint64_t shape = 0, material = 0, teleport = 0;
			PhysicsBodyDesc desc;
		};
		struct CharacterLink {
			PhysicsBodyHandle handle;
			EntityReference owner;
			uint64_t teleport = 0;
			CharacterSettings settings;
		};
		struct RefLess {
			bool operator()(const EntityReference& a, const EntityReference& b) const {
				return std::tie(a.world, a.persistent, a.resolved.index, a.resolved.generation) <
				    std::tie(b.world, b.persistent, b.resolved.index, b.resolved.generation);
			}
		};
		std::map<EntityReference, BodyLink, RefLess> bodies;
		std::map<PersistentEntityId, CharacterLink> characters;
		std::vector<std::string> diagnostics;
		std::vector<PhysicsContactEvent> events;
	};

	PhysicsService::PhysicsService() : m_Impl(std::make_unique<Impl>()) {}

	PhysicsService::~PhysicsService() {
		Stop();
	}

	bool PhysicsService::ConfigureFromJson(std::string_view source, std::string& error) try {
		if (m_Impl->world) {
			error = "Stop the physics world before changing its configuration";
			return false;
		}
		const auto json = AuthorJson::Parse(source, 65536);
		AuthorJson::Fields(json,
		    {"schemaVersion", "maxBodies", "maxContacts", "maxEvents", "scratchBytes", "gravity", "collisionMasks"});
		if (AuthorJson::Unsigned(json.at("schemaVersion")) != 1)
			throw std::invalid_argument("Unsupported physics defaults schema");
		PhysicsWorldConfig value;
		const auto bodies = AuthorJson::Unsigned(json.at("maxBodies")),
		           contacts = AuthorJson::Unsigned(json.at("maxContacts")),
		           events = AuthorJson::Unsigned(json.at("maxEvents")),
		           scratch = AuthorJson::Unsigned(json.at("scratchBytes"));
		if (bodies > UINT32_MAX || contacts > UINT32_MAX || events > UINT32_MAX || scratch > SIZE_MAX)
			throw std::invalid_argument("Physics capacity integer overflow");
		value.maxBodies = uint32_t(bodies);
		value.maxContacts = uint32_t(contacts);
		value.maxEvents = uint32_t(events);
		value.scratchBytes = size_t(scratch);
		value.gravity = AuthorJson::Vec3(json.at("gravity"));
		const auto& masks = json.at("collisionMasks");
		if (!masks.is_array() || masks.size() != 16)
			throw std::invalid_argument("Physics requires 16 collision masks");
		for (size_t i = 0; i < 16; ++i) {
			const auto mask = AuthorJson::Unsigned(masks[i]);
			if (mask > UINT16_MAX)
				throw std::invalid_argument("Collision mask exceeds 16 bits");
			value.collisionMasks[i] = uint16_t(mask);
		}
		if (!ValidatePhysicsWorldConfig(value, error))
			return false;
		m_Impl->config = value;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	void PhysicsService::Stop() noexcept {
		m_Impl->characters.clear();
		m_Impl->bodies.clear();
		m_Impl->world.reset();
		m_Impl->scene = 0;
		m_Impl->sequence = 0;
		m_Impl->dropped = 0;
		m_Impl->events.clear();
		m_Impl->diagnostics.clear();
	}

	PhysicsWorld* PhysicsService::World() noexcept {
		return m_Impl->world.get();
	}

	bool PhysicsService::EnsureWorld(Scene& scene, std::string& error) try {
		if (m_Impl->scene != scene.GetWorld().GetIdentity()) {
			Stop();
			m_Impl->scene = scene.GetWorld().GetIdentity();
		}
		if (!m_Impl->world) {
			auto next = std::make_unique<PhysicsWorld>();
			if (!next->Initialize(m_Impl->config, error))
				return false;
			m_Impl->world = std::move(next);
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	const std::vector<std::string>& PhysicsService::Diagnostics() const noexcept {
		return m_Impl->diagnostics;
	}

	void PhysicsService::TakeEvents(std::vector<PhysicsContactEvent>& out) {
		out.clear();
		out.swap(m_Impl->events);
		m_Impl->events.clear();
	}

	bool PhysicsService::Tick(Scene& scene, AssetManager& assets, double dt, uint64_t tick, std::string& error) try {
		if (!std::isfinite(dt) || dt <= 0 || dt > 0.1) {
			error = "Physics fixed step must be seconds in (0,0.1]";
			return false;
		}
		if (m_Impl->scene != scene.GetWorld().GetIdentity()) {
			Stop();
			m_Impl->scene = scene.GetWorld().GetIdentity();
		}
		m_Impl->diagnostics.clear();
		/* 先应用已排队的场景修改并更新世界变换，使物理输入与当前层级一致。 */
		const auto prepared = scene.CommitPending();
		if (!prepared.IsSuccess()) {
			error = prepared.message;
			return false;
		}
		const auto transformed = TransformSystem::Update(scene);
		if (!transformed.IsSuccess()) {
			error = transformed.message;
			return false;
		}
		struct Input {
			EntityId entity;
			EntityReference ref;
			TransformComponent transform;
			RelationshipComponent relationship;
			std::optional<RigidBodyComponent> body;
			std::optional<ColliderComponent> collider;
			std::optional<CharacterControllerComponent> character;
		};
		std::vector<Input> inputs;
		const auto snapshot = scene.GetWorld().ForEach([&](EntityId id) {
			const auto& w = scene.GetWorld();
			if (!w.Has<RigidBodyComponent>(id) && !w.Has<CharacterControllerComponent>(id))
				return WorldStatus{};
			Input input;
			input.entity = id;
			input.ref = scene.MakeReference(id);
			auto status = w.Get(id, input.transform);
			if (!status.IsSuccess())
				return status;
			status = w.Get(id, input.relationship);
			if (!status.IsSuccess())
				return status;
			if (w.Has<RigidBodyComponent>(id)) {
				RigidBodyComponent value;
				status = w.Get(id, value);
				if (!status.IsSuccess())
					return status;
				input.body = value;
			}
			if (w.Has<ColliderComponent>(id)) {
				ColliderComponent value;
				status = w.Get(id, value);
				if (!status.IsSuccess())
					return status;
				input.collider = value;
			}
			if (w.Has<CharacterControllerComponent>(id)) {
				CharacterControllerComponent value;
				status = w.Get(id, value);
				if (!status.IsSuccess())
					return status;
				input.character = value;
			}
			if (inputs.size() == 4096)
				return WorldStatus{WorldError::CapacityExceeded, "Physics component snapshot exceeds 4096"};
			inputs.push_back(std::move(input));
			return WorldStatus{};
		});
		if (!snapshot.IsSuccess()) {
			error = snapshot.message;
			return false;
		}
		if (inputs.empty() && !m_Impl->world) {
			error.clear();
			return true;
		}
		if (!m_Impl->world) {
			auto next = std::make_unique<PhysicsWorld>();
			if (!next->Initialize(m_Impl->config, error))
				return false;
			m_Impl->world = std::move(next);
			m_Impl->scene = scene.GetWorld().GetIdentity();
		}
		std::set<EntityReference, Impl::RefLess> alive;
		std::set<PersistentEntityId> activeCharacters;
		auto diagnostic = [&](const Input& i, std::string message) {
			if (m_Impl->diagnostics.size() < 256)
				m_Impl->diagnostics.push_back(i.ref.persistent.ToString() + ": " + std::move(message));
		};
		for (const auto& i : inputs) {
			if (i.body && i.character) {
				diagnostic(i, "Character and rigid body both claim Transform; neither is admitted");
				continue;
			}
			/* 动态父子同时回写需要联合约束求解，首版明确拒绝，不能使用旧parent矩阵静默算错局部姿态。 */
			bool movingParent = false;
			auto parentId = i.relationship.parent;
			for (size_t depth = 0; parentId.HasValue() && depth < 4096; ++depth) {
				RigidBodyComponent parentBody;
				CharacterControllerComponent parentCharacter;
				RelationshipComponent parent;
				if (scene.GetWorld().Has<RigidBodyComponent>(parentId) &&
				    scene.GetWorld().Get(parentId, parentBody).IsSuccess() && parentBody.enabled &&
				    parentBody.motion != PhysicsMotion::Static)
					movingParent = true;
				if (scene.GetWorld().Has<CharacterControllerComponent>(parentId) &&
				    scene.GetWorld().Get(parentId, parentCharacter).IsSuccess() && parentCharacter.enabled)
					movingParent = true;
				if (!scene.GetRelationship(parentId, parent).IsSuccess()) {
					movingParent = true;
					break;
				}
				parentId = parent.parent;
			}
			if (movingParent) {
				diagnostic(i, "Physics body/character must not have an active moving physics ancestor");
				continue;
			}
			Math::TransformTRS worldPose;
			if (!Math::DecomposeTRS(i.transform.worldMatrix, worldPose).IsSuccess()) {
				diagnostic(i, "Physics requires nonsheared invertible world TRS");
				continue;
			}
			const PhysicsPose pose{worldPose.translation, worldPose.rotation};
			if (i.body && i.body->enabled) {
				if (!i.collider || !i.collider->shape.HasValue()) {
					diagnostic(i, "Rigid body requires collider");
					continue;
				}
				if (m_Impl->bodies.contains(i.ref))
					alive.insert(i.ref);
				AssetVersionRef<CollisionShapeAsset> shape;
				AssetVersionRef<PhysicsMaterialAsset> material;
				bool fallback = false;
				if (i.collider->shapeVersion.HasValue() && i.collider->shapeVersion.GetId() == i.collider->shape.id)
					shape = i.collider->shapeVersion;
				else if ((assets.GetStatus(i.collider->shape.id) == AssetStatus::Unloaded &&
				             !assets.Request(i.collider->shape.id, error)) ||
				    !assets.Acquire(i.collider->shape.id, shape, fallback, error) || fallback) {
					diagnostic(i, "Waiting for exact collision shape: " + error);
					continue;
				}
				PhysicsMaterialAsset parameters;
				if (i.collider->material.HasValue()) {
					if (i.collider->materialVersion.HasValue() &&
					    i.collider->materialVersion.GetId() == i.collider->material.id)
						material = i.collider->materialVersion;
					else if ((assets.GetStatus(i.collider->material.id) == AssetStatus::Unloaded &&
					             !assets.Request(i.collider->material.id, error)) ||
					    !assets.Acquire(i.collider->material.id, material, fallback, error) || fallback) {
						diagnostic(i, "Waiting for exact physics material: " + error);
						continue;
					}
					parameters = *material.Get();
				}
				PhysicsBodyDesc desc{i.ref, i.body->motion, pose, worldPose.scale, i.body->initialVelocity,
				    i.body->massKg, i.body->gravityFactor, i.body->collisionGroup, i.collider->sensor};
				auto found = m_Impl->bodies.find(i.ref);
				const auto materialVersion = material.HasValue() ? material.GetVersion() : 0;
				const bool rebuild = found == m_Impl->bodies.end() || found->second.shapeId != shape.GetId() ||
				    found->second.materialId != material.GetId() || found->second.shape != shape.GetVersion() ||
				    found->second.material != materialVersion || found->second.desc.motion != desc.motion ||
				    found->second.desc.massKg != desc.massKg ||
				    found->second.desc.gravityFactor != desc.gravityFactor || found->second.desc.scale != desc.scale ||
				    found->second.desc.collisionGroup != desc.collisionGroup ||
				    found->second.desc.sensor != desc.sensor;
				if (rebuild) {
					PhysicsBodyHandle body;
					if (found != m_Impl->bodies.end()) {
						PhysicsBodyState previous;
						if (m_Impl->world->ReadBody(found->second.handle, previous, error))
							desc.linearVelocity = previous.linearVelocity;
					}
					if (!m_Impl->world->CreateBody(desc, *shape.Get(), parameters, body, error)) {
						diagnostic(i, error);
						if (found != m_Impl->bodies.end())
							alive.insert(i.ref);
						continue;
					}
					if (found != m_Impl->bodies.end())
						m_Impl->world->DestroyBody(found->second.handle, error);
					m_Impl->bodies.insert_or_assign(i.ref,
					    Impl::BodyLink{body, shape.GetId(), material.GetId(), shape.GetVersion(), materialVersion,
					        i.body->teleportRevision, desc});
					found = m_Impl->bodies.find(i.ref);
				}
				alive.insert(i.ref);
				if (!i.collider->shapeVersion.HasValue() || i.collider->shapeVersion.GetId() != shape.GetId() ||
				    i.collider->shapeVersion.GetVersion() != shape.GetVersion() ||
				    (material.HasValue() &&
				        (!i.collider->materialVersion.HasValue() ||
				            i.collider->materialVersion.GetId() != material.GetId() ||
				            i.collider->materialVersion.GetVersion() != material.GetVersion()))) {
					auto pinned = *i.collider;
					pinned.shapeVersion = shape;
					pinned.materialVersion = material;
					const auto status = SetCollider(scene, i.entity, pinned);
					if (!status.IsSuccess()) {
						error = status.message;
						return false;
					}
				}
				const bool teleport = found->second.teleport != i.body->teleportRevision;
				if (teleport || desc.motion != PhysicsMotion::Dynamic) {
					if (!m_Impl->world->SetPose(found->second.handle, pose, teleport, dt, error)) {
						diagnostic(i, error);
						continue;
					}
					found->second.teleport = i.body->teleportRevision;
				}
			}
			if (i.character && i.character->enabled) {
				if (worldPose.scale != Math::Vec3{1}) {
					diagnostic(i, "Character capsule settings are in metres and require unit world scale");
					continue;
				}
				activeCharacters.insert(i.ref.persistent);
				auto found = m_Impl->characters.find(i.ref.persistent);
				/* 配置改变先创建候选，成功后才退休旧角色。 */
				const auto& settings = i.character->settings;
				const auto differs = [&](const CharacterSettings& a, const CharacterSettings& b) {
					return a.radius != b.radius || a.halfHeight != b.halfHeight ||
					    a.maxSlopeRadians != b.maxSlopeRadians || a.stepHeight != b.stepHeight ||
					    a.snapDistance != b.snapDistance || a.massKg != b.massKg ||
					    a.collisionGroup != b.collisionGroup;
				};
				if (found == m_Impl->characters.end() || found->second.owner != i.ref ||
				    differs(found->second.settings, settings)) {
					PhysicsBodyHandle character;
					if (!m_Impl->world->CreateCharacter(settings, pose, character, error,
					        found == m_Impl->characters.end() ? PhysicsBodyHandle{} : found->second.handle, i.ref)) {
						diagnostic(i, error);
						continue;
					}
					if (found != m_Impl->characters.end())
						m_Impl->world->DestroyCharacter(found->second.handle, error);
					m_Impl->characters.insert_or_assign(i.ref.persistent,
					    Impl::CharacterLink{character, i.ref, i.character->teleportRevision, settings});
					found = m_Impl->characters.find(i.ref.persistent);
				}
				if (found->second.teleport != i.character->teleportRevision) {
					if (!m_Impl->world->TeleportCharacter(found->second.handle, pose, error)) {
						diagnostic(i, error);
						continue;
					}
					found->second.teleport = i.character->teleportRevision;
				}
				CharacterMotion motion;
				if (!m_Impl->world->MoveCharacter(
				        found->second.handle, i.character->desiredVelocity, dt, motion, error)) {
					diagnostic(i, error);
					continue;
				}
				/* y为一次跳跃脉冲；持续水平意图保留，防止按住一次输入就自动连跳。 */
				if (i.character->desiredVelocity.y != 0) {
					auto consumed = *i.character;
					consumed.desiredVelocity.y = 0;
					const auto status = SetCharacterController(scene, i.entity, consumed);
					if (!status.IsSuccess()) {
						error = status.message;
						return false;
					}
				}
				worldPose.translation = motion.pose.position;
				worldPose.rotation = motion.pose.rotation;
				Math::Mat4 matrix;
				if (!Math::ComposeTRS(worldPose, matrix).IsSuccess()) {
					diagnostic(i, "Character world pose cannot be composed");
					continue;
				}
				if (i.relationship.parent.HasValue()) {
					TransformComponent parent;
					Math::Mat4 inverse;
					if (!scene.GetTransform(i.relationship.parent, parent).IsSuccess() ||
					    !Math::InverseAffine(parent.worldMatrix, inverse).IsSuccess() ||
					    !Math::DecomposeTRS(inverse * matrix, worldPose).IsSuccess()) {
						diagnostic(i, "Character parent transform is not invertible TRS");
						continue;
					}
				}
				if (const auto status = scene.QueueSetLocal(i.entity, worldPose); !status.IsSuccess()) {
					error = status.message;
					return false;
				}
			}
		}
		for (auto it = m_Impl->bodies.begin(); it != m_Impl->bodies.end();)
			if (!alive.contains(it->first)) {
				m_Impl->world->DestroyBody(it->second.handle, error);
				it = m_Impl->bodies.erase(it);
			} else
				++it;
		for (auto it = m_Impl->characters.begin(); it != m_Impl->characters.end();)
			if (!activeCharacters.contains(it->first)) {
				m_Impl->world->DestroyCharacter(it->second.handle, error);
				it = m_Impl->characters.erase(it);
			} else
				++it;
		if (m_Impl->sequence == UINT64_MAX) {
			error = "Physics internal tick exhausted";
			return false;
		}
		/* 刚体与角色已同步到后端，推进一次物理步并将动态刚体姿态写回场景。 */
		if (!m_Impl->world->Step(dt, ++m_Impl->sequence, error))
			return false;
		for (const auto& i : inputs)
			if (i.body && i.body->enabled) {
				const auto link = m_Impl->bodies.find(i.ref);
				if (link == m_Impl->bodies.end() || link->second.desc.motion != PhysicsMotion::Dynamic)
					continue;
				PhysicsBodyState value;
				if (!m_Impl->world->ReadBody(link->second.handle, value, error))
					return false;
				Math::TransformTRS local;
				local.translation = value.pose.position;
				local.rotation = value.pose.rotation;
				local.scale = link->second.desc.scale;
				Math::Mat4 matrix;
				if (!Math::ComposeTRS(local, matrix).IsSuccess()) {
					diagnostic(i, "Dynamic pose cannot be represented");
					continue;
				}
				if (i.relationship.parent.HasValue()) {
					TransformComponent parent;
					Math::Mat4 inverse;
					if (!scene.GetTransform(i.relationship.parent, parent).IsSuccess() ||
					    !Math::InverseAffine(parent.worldMatrix, inverse).IsSuccess() ||
					    !Math::DecomposeTRS(inverse * matrix, local).IsSuccess()) {
						diagnostic(i, "Dynamic parent pose cannot be represented as local TRS");
						continue;
					}
				}
				const auto status = scene.QueueSetLocal(i.entity, local);
				if (!status.IsSuccess()) {
					error = status.message;
					return false;
				}
			}
		std::vector<PhysicsContactEvent> events;
		/* 收集后端接触事件，核对丢失计数并按稳定顺序交给场景消费者。 */
		m_Impl->world->TakeEvents(events);
		const auto dropped = m_Impl->world->DroppedEvents();
		if (dropped != m_Impl->dropped) {
			if (m_Impl->diagnostics.size() < 256)
				m_Impl->diagnostics.push_back(
				    "Physics contact/event capacity exhausted; dropped total=" + std::to_string(dropped));
			m_Impl->dropped = dropped;
		}
		/* 角色接触在刚体Step前产生；统一到宿主tick后重新排序，不能沿用两个内部tick的旧顺序。 */
		for (auto& e : events)
			e.tick = tick;
		std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
			return std::tie(a.firstBody.value, a.secondBody.value, a.kind) <
			    std::tie(b.firstBody.value, b.secondBody.value, b.kind);
		});
		for (const auto& e : events) {
			if (m_Impl->events.size() >= m_Impl->config.maxEvents) {
				if (m_Impl->diagnostics.size() < 256)
					m_Impl->diagnostics.push_back(
					    "Physics event consumer backlog exceeded its configured capacity; event dropped");
				break;
			}
			m_Impl->events.push_back(e);
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
} /* 命名空间 Catalpa */
