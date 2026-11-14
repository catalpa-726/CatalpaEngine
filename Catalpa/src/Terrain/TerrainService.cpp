#include "catpch.h"

#include "Terrain/TerrainService.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Core/Jobs/JobSystem.h"
#include "Scene/Scene.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/Components/RigidBodyComponent.h"
#include "Scene/Components/CharacterControllerComponent.h"

namespace Catalpa {
	struct TerrainService::Impl {
		/* 预算计入候选工作区的内存。预留额度通过共享所有权的别名指针绑定到帧快照，最后一份快照释放时才归还。 */
		MemoryBudget budget{128 * 1024 * 1024};
		struct Lease {
			MemoryReservation reservation;
			std::shared_ptr<const MeshAsset> mesh;
			std::shared_ptr<const CollisionShapeAsset> collision;
		};
		struct Work {
			EntityReference owner;
			uint32_t x = 0, z = 0, lod = 0;
			uint64_t version = 0;
			AssetVersionRef<TerrainAsset> terrain;
			AssetVersionRef<MaterialInstance> material;
			std::shared_ptr<Lease> lease;
			AssetVersionRef<MeshAsset> result;
			JobHandle job;
			std::string error;
			bool success = false;
		};
		struct Slot {
			EntityReference owner;
			uint32_t x = 0, z = 0, lod = 0, targetLod = 0;
			uint64_t version = 0, targetVersion = 0;
			AssetVersionRef<MeshAsset> mesh;
			AssetVersionRef<MaterialInstance> material;
			std::shared_ptr<const CollisionShapeAsset> collision;
			PhysicsBodyHandle body;
			Math::Vec3 bodyScale{1};
			Math::Mat4 world{1};
			bool collisionEnabled = false;
		};
		struct Owner {
			EntityReference ref;
			AssetVersionRef<TerrainAsset> asset;
			AssetVersionRef<MaterialInstance> material;
		};
		using Key = std::tuple<PersistentEntityId, uint32_t, uint32_t>;
		uint64_t world = 0;
		JobSystem* jobs = nullptr;
		PhysicsWorld* physics = nullptr;
		std::map<PersistentEntityId, Owner> owners;
		std::map<Key, Slot> slots;
		std::vector<std::shared_ptr<Work>> pending;
		std::vector<TerrainTileDraw> draws;
		std::vector<std::string> diagnostics;

		void Diagnose(std::string text) {
			if (diagnostics.size() < 256)
				diagnostics.push_back(std::move(text));
		}

		void RetireBody(Slot& slot) noexcept {
			if (slot.body.HasValue() && physics) {
				std::string ignored;
				physics->DestroyBody(slot.body, ignored);
			}
			slot.body = {};
		}
	};

	TerrainService::TerrainService() : m_Impl(std::make_unique<Impl>()) {}

	TerrainService::~TerrainService() {
		Stop();
	}

	void TerrainService::Stop() noexcept {
		for (auto& work : m_Impl->pending)
			work->job.Cancel();
		if (m_Impl->jobs)
			for (auto& work : m_Impl->pending)
				if (!m_Impl->jobs->IsComplete(work->job)) {
					try {
						std::string ignored;
						(void)m_Impl->jobs->Wait(work->job, ignored);
					} catch (...) {
						/* Wait先join再复制诊断；关闭不因诊断分配失败遗留活worker。 */
					}
					if (!m_Impl->jobs->IsComplete(work->job))
						std::terminate();
				}
		for (auto& [key, slot] : m_Impl->slots)
			m_Impl->RetireBody(slot);
		if (m_Impl->jobs)
			m_Impl->jobs->CollectCompleted();
		m_Impl->pending.clear();
		m_Impl->slots.clear();
		m_Impl->owners.clear();
		m_Impl->draws.clear();
		m_Impl->diagnostics.clear();
		m_Impl->jobs = nullptr;
		m_Impl->physics = nullptr;
		m_Impl->world = 0;
	}

	const std::vector<TerrainTileDraw>& TerrainService::Draws() const noexcept {
		return m_Impl->draws;
	}

	const std::vector<std::string>& TerrainService::Diagnostics() const noexcept {
		return m_Impl->diagnostics;
	}

	MemoryBudgetStats TerrainService::Budget() const noexcept {
		return m_Impl->budget.GetStats();
	}

	bool TerrainService::Tick(Scene& scene, AssetManager& assets, JobSystem& jobs, Math::Vec3 camera,
	    PhysicsWorld* physics, std::string& error) try {
		if (!jobs.IsRunning() || !std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) {
			error = "Terrain needs running owner Jobs and finite camera position";
			return false;
		}
		if (m_Impl->world != scene.GetWorld().GetIdentity() || m_Impl->jobs != &jobs) {
			Stop();
			m_Impl->world = scene.GetWorld().GetIdentity();
			m_Impl->jobs = &jobs;
		}
		if (m_Impl->physics != physics) {
			for (auto& [key, slot] : m_Impl->slots)
				m_Impl->RetireBody(slot);
			m_Impl->physics = physics;
		}
		m_Impl->diagnostics.clear();
		m_Impl->draws.clear();
		auto status = scene.CommitPending();
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		status = TransformSystem::Update(scene);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		struct Input {
			EntityReference owner;
			TerrainComponent component;
			Math::Mat4 world;
		};
		std::vector<Input> inputs;
		status = scene.GetWorld().ForEach([&](EntityId id) -> WorldStatus {
			const auto& w = scene.GetWorld();
			if (!w.Has<TerrainComponent>(id))
				return {};
			TerrainComponent value;
			auto s = w.Get(id, value);
			if (!s.IsSuccess())
				return s;
			if (!value.enabled)
				return {};
			if (!value.asset.HasValue() || !std::isfinite(value.viewDistance) || !std::isfinite(value.lodDistance) ||
			    value.lodDistance < 1 || value.viewDistance < value.lodDistance || value.viewDistance > 100000)
				return {WorldError::InvalidArgument, "Terrain component has invalid identity or LOD distances"};
			TransformComponent transform;
			s = w.Get(id, transform);
			if (!s.IsSuccess())
				return s;
			if (value.collisionEnabled) {
				auto ancestor = id;
				bool conflicts = false;
				for (size_t depth = 0; ancestor.HasValue() && depth < 4096; ++depth) {
					RigidBodyComponent body;
					CharacterControllerComponent character;
					RelationshipComponent relationship;
					if (w.Has<RigidBodyComponent>(ancestor) && w.Get(ancestor, body).IsSuccess() && body.enabled &&
					    (ancestor == id || body.motion != PhysicsMotion::Static))
						conflicts = true;
					if (w.Has<CharacterControllerComponent>(ancestor) && w.Get(ancestor, character).IsSuccess() &&
					    character.enabled)
						conflicts = true;
					if (!scene.GetRelationship(ancestor, relationship).IsSuccess()) {
						conflicts = true;
						break;
					}
					ancestor = relationship.parent;
				}
				if (conflicts) {
					value.collisionEnabled = false;
					m_Impl->Diagnose("Terrain static collision cannot share a rigid-body/character Transform or a "
					                 "moving physics ancestor; terrain drawing remains active");
				}
			}
			if (inputs.size() == 16)
				return {WorldError::CapacityExceeded, "Terrain scene owner budget reached 16"};
			inputs.push_back({scene.MakeReference(id), value, transform.worldMatrix});
			return {};
		});
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		struct Wanted {
			Impl::Key key;
			const Input* input;
			TerrainTileRecord tile;
			AssetVersionRef<TerrainAsset> terrain;
			AssetVersionRef<MaterialInstance> material;
			float distance = 0;
		};
		std::vector<Wanted> candidates;
		std::set<PersistentEntityId> alive;
		for (const auto& input : inputs) {
			alive.insert(input.owner.persistent);
			auto& owner = m_Impl->owners[input.owner.persistent];
			if (owner.ref != input.owner)
				owner = {input.owner, {}, {}};
			AssetVersionRef<TerrainAsset> terrain;
			AssetVersionRef<MaterialInstance> material;
			bool fallback = false;
			bool acceptedRequestedVersion = false;
			const bool pinned = input.component.assetVersion.HasValue() &&
			    input.component.assetVersion.GetId() == input.component.asset.id;
			if (pinned)
				terrain = input.component.assetVersion;
			const auto requested = pinned || assets.GetStatus(input.component.asset.id) != AssetStatus::Unloaded ||
			    assets.Request(input.component.asset.id, error);
			if (requested && (pinned || assets.Acquire(input.component.asset.id, terrain, fallback, error)) &&
			    !fallback) {
				if (terrain.Get()->material.id.HasValue() &&
				    assets.GetStatus(terrain.Get()->material.id) == AssetStatus::Unloaded)
					assets.Request(terrain.Get()->material.id, error);
				if (input.component.materialVersion.HasValue() &&
				    input.component.materialVersion.GetId() == terrain.Get()->material.id &&
				    input.component.materialVersion.GetVersion() == terrain.Get()->material.version)
					material = input.component.materialVersion;
				else if (owner.asset.HasValue() && owner.asset.GetId() == terrain.GetId() &&
				    owner.asset.GetVersion() == terrain.GetVersion())
					material = owner.material;
				else if (!assets.Acquire(terrain.Get()->material.id, material, fallback, error) || fallback ||
				    material.GetVersion() != terrain.Get()->material.version)
					material = {};
				if (material.HasValue()) {
					owner.asset = std::move(terrain);
					owner.material = std::move(material);
					acceptedRequestedVersion = true;
				} else
					m_Impl->Diagnose(
					    "Terrain waits for exact material; previous tile versions remain active: " + error);
			} else
				m_Impl->Diagnose("Terrain waits for metadata: " + error);
			if (!owner.asset.HasValue())
				continue;
			/* 旧画面可在新元数据/材质等待期间保留，但不能把旧版本pin写进新请求的作者组件。 */
			if (acceptedRequestedVersion &&
			    (!pinned || !input.component.materialVersion.HasValue() ||
			        input.component.materialVersion.GetId() != owner.material.GetId() ||
			        input.component.materialVersion.GetVersion() != owner.material.GetVersion())) {
				auto bound = input.component;
				bound.assetVersion = owner.asset;
				bound.materialVersion = owner.material;
				const auto status = SetTerrain(scene, input.owner.resolved, bound);
				if (!status.IsSuccess()) {
					error = status.message;
					return false;
				}
			}
			const auto& metadata = *owner.asset.Get();
			const auto& s = metadata.settings;
			for (const auto& tile : metadata.tiles)
				if (tile.lod == 0) {
					const auto center = Math::Vec3(input.world *
					    Math::Vec4((tile.bounds.minimum.x + tile.bounds.maximum.x) * 0.5f, tile.bounds.maximum.y,
					        (tile.bounds.minimum.z + tile.bounds.maximum.z) * 0.5f, 1));
					const float distance = glm::length(center - camera);
					if (!std::isfinite(distance) || distance > input.component.viewDistance)
						continue;
					const auto lod = uint32_t(
					    std::clamp(std::floor(std::log2(std::max(distance / input.component.lodDistance, 1.0f))), 0.0f,
					        float(s.lodCount - 1)));
					const auto found = std::find_if(metadata.tiles.begin(), metadata.tiles.end(),
					    [&](const auto& t) { return t.x == tile.x && t.z == tile.z && t.lod == lod; });
					if (found != metadata.tiles.end())
						candidates.push_back({{input.owner.persistent, tile.x, tile.z}, &input, *found, owner.asset,
						    owner.material, distance});
				}
		}
		for (auto it = m_Impl->owners.begin(); it != m_Impl->owners.end();)
			if (!alive.contains(it->first))
				it = m_Impl->owners.erase(it);
			else
				++it;
		std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
			return a.distance == b.distance ? a.key < b.key : a.distance < b.distance;
		});
		if (candidates.size() > 64) {
			candidates.resize(64);
			m_Impl->Diagnose("Terrain visible tile budget reached 64; far tiles omitted");
		}
		std::set<Impl::Key> wanted;
		for (const auto& candidate : candidates) {
			wanted.insert(candidate.key);
			auto& slot = m_Impl->slots[candidate.key];
			if (slot.owner != candidate.input->owner) {
				m_Impl->RetireBody(slot);
				slot = {};
				slot.owner = candidate.input->owner;
				slot.x = candidate.tile.x;
				slot.z = candidate.tile.z;
			}
			slot.targetVersion = candidate.terrain.GetVersion();
			slot.targetLod = candidate.tile.lod;
			slot.world = candidate.input->world;
			slot.collisionEnabled = candidate.input->component.collisionEnabled;
		}
		for (auto it = m_Impl->slots.begin(); it != m_Impl->slots.end();)
			if (!wanted.contains(it->first)) {
				m_Impl->RetireBody(it->second);
				it = m_Impl->slots.erase(it);
			} else
				++it;
		for (auto it = m_Impl->pending.begin(); it != m_Impl->pending.end();) {
			const auto& work = *it;
			auto slot = m_Impl->slots.find({work->owner.persistent, work->x, work->z});
			const bool current = slot != m_Impl->slots.end() && slot->second.owner == work->owner &&
			    slot->second.targetVersion == work->version && slot->second.targetLod == work->lod;
			if (!current)
				work->job.Cancel();
			if (!jobs.IsComplete(work->job)) {
				++it;
				continue;
			}
			if (current && work->success) {
				/* 新CPU版本完整通过后才退休旧碰撞和网格；旧帧快照自行保活退休数据。 */
				auto& value = slot->second;
				PhysicsBodyHandle candidateBody;
				Math::TransformTRS pose;
				bool admitted = true;
				if (physics && value.collisionEnabled && work->lease->collision) {
					admitted = Math::DecomposeTRS(value.world, pose).IsSuccess() && pose.scale.x > 0 &&
					    pose.scale.y > 0 && pose.scale.z > 0;
					if (admitted) {
						PhysicsBodyDesc desc;
						desc.entity = value.owner;
						desc.pose = {pose.translation, pose.rotation};
						desc.scale = pose.scale;
						admitted = physics->CreateBody(desc, *work->lease->collision, {}, candidateBody, work->error);
					}
				}
				if (admitted) {
					m_Impl->RetireBody(value);
					value.body = candidateBody;
					value.bodyScale = pose.scale;
					value.version = work->version;
					value.lod = work->lod;
					value.mesh = work->result;
					value.material = work->material;
					if (work->lease->collision)
						value.collision =
						    std::shared_ptr<const CollisionShapeAsset>(work->lease, work->lease->collision.get());
					else
						value.collision.reset();
				} else
					m_Impl->Diagnose(
					    "Terrain replacement collider failed; previous CPU/SDK version retained: " + work->error);
			} else if (current)
				m_Impl->Diagnose("Terrain tile read/decode failed; previous version retained: " +
				    (work->error.empty() ? work->job.GetError() : work->error));
			it = m_Impl->pending.erase(it);
		}
		jobs.CollectCompleted();
		for (const auto& candidate : candidates) {
			auto& slot = m_Impl->slots.at(candidate.key);
			if (slot.version == slot.targetVersion && slot.lod == slot.targetLod)
				continue;
			if (std::any_of(m_Impl->pending.begin(), m_Impl->pending.end(), [&](const auto& w) {
				    return w->owner == slot.owner && w->x == slot.x && w->z == slot.z &&
				        w->version == slot.targetVersion && w->lod == slot.targetLod;
			    }))
				continue;
			if (m_Impl->pending.size() >= 2)
				break;
			auto lease = std::make_shared<Impl::Lease>();
			if (!m_Impl->budget.TryReserve(48 * 1024 * 1024, lease->reservation)) {
				m_Impl->Diagnose("Terrain CPU budget held by active/retired snapshots; tile request deferred");
				break;
			}
			auto work = std::make_shared<Impl::Work>();
			work->owner = slot.owner;
			work->x = slot.x;
			work->z = slot.z;
			work->lod = slot.targetLod;
			work->version = slot.targetVersion;
			work->terrain = candidate.terrain;
			work->material = candidate.material;
			work->lease = lease;
			const auto tile = candidate.tile;
			/* 在作业系统接受任务前先为任务登记表预留容量，避免工作已提交却因登记失败而无法取消或等待。 */
			m_Impl->pending.reserve(2);
			if (!jobs.Submit(
			        [work, tile](const CancellationToken& token) {
				        const auto& metadata = *work->terrain.Get();
				        const auto read = [&](const AssetDependency& reference, const char* type, AssetContainer& out) {
					        if (token.IsCancellationRequested()) {
						        work->error = "Terrain tile cancelled";
						        return false;
					        }
					        std::vector<std::byte> bytes;
					        if (!metadata.readDeferred(reference.id, 4 * 1024 * 1024, bytes, work->error) ||
					            !AssetContainerReader::Read(bytes, out, work->error, 4 * 1024 * 1024))
						        return false;
					        if (out.id != reference.id || out.version != reference.version ||
					            out.type.ToString() != type || out.payloadVersion != 1 || !out.dependencies.empty()) {
						        work->error = "Terrain tile exact identity/type/version/dependencies mismatch";
						        return false;
					        }
					        return true;
				        };
				        AssetContainer container;
				        if (!read(tile.mesh, "mesh", container))
					        return;
				        AssetDecodeContext context;
				        context.cancellation = token;
				        context.maxResidentBytes = 4 * 1024 * 1024;
				        AssetLoadResult decoded;
				        if (!GeometryAssetLoader::DecodeMesh(container.payload, context, decoded, work->error) ||
				            decoded.cppType != typeid(MeshAsset))
					        return;
				        work->lease->mesh = std::static_pointer_cast<const MeshAsset>(decoded.value);
				        size_t resident = decoded.residentBytes;
				        if (tile.collision.id.HasValue()) {
					        if (!read(tile.collision, "collision_shape", container))
						        return;
					        auto shape = std::make_shared<CollisionShapeAsset>();
					        if (!ReadCollisionShape(
					                {reinterpret_cast<const char*>(container.payload.data()), container.payload.size()},
					                *shape, work->error))
						        return;
					        resident += sizeof(CollisionShapeAsset) + shape->vertices.capacity() * sizeof(Math::Vec3) +
					            shape->indices.capacity() * 4;
					        work->lease->collision = std::move(shape);
				        }
				        if (token.IsCancellationRequested()) {
					        work->error = "Terrain tile cancelled before publication";
					        return;
				        }
				        if (!work->lease->reservation.Shrink(resident)) {
					        work->error = "Terrain decoded resident data exceeds reservation";
					        return;
				        }
				        auto owned = std::shared_ptr<const MeshAsset>(work->lease, work->lease->mesh.get());
				        work->success = AssetVersionRef<MeshAsset>::TryCreate(
				            tile.mesh.id, tile.mesh.version, std::move(owned), work->result, work->error);
			        },
			        work->job, error)) {
				m_Impl->Diagnose(error);
				continue;
			}
			m_Impl->pending.push_back(std::move(work));
		}
		for (auto& [key, slot] : m_Impl->slots) {
			if (!slot.mesh.HasValue())
				continue;
			if (!slot.collisionEnabled || !physics || !slot.collision)
				m_Impl->RetireBody(slot);
			else {
				Math::TransformTRS pose;
				if (!Math::DecomposeTRS(slot.world, pose).IsSuccess() || pose.scale.x <= 0 || pose.scale.y <= 0 ||
				    pose.scale.z <= 0) {
					m_Impl->RetireBody(slot);
					m_Impl->Diagnose("Terrain collision requires positive nonsheared TRS; rendering snapshot "
					                 "remains independent");
				} else if (!slot.body.HasValue() || slot.bodyScale != pose.scale) {
					PhysicsBodyDesc desc;
					desc.entity = slot.owner;
					desc.pose = {pose.translation, pose.rotation};
					desc.scale = pose.scale;
					PhysicsBodyHandle body;
					if (physics->CreateBody(desc, *slot.collision, {}, body, error)) {
						m_Impl->RetireBody(slot);
						slot.body = body;
						slot.bodyScale = pose.scale;
					} else
						m_Impl->Diagnose("Terrain collider candidate failed: " + error);
				} else if (!physics->SetPose(slot.body, {pose.translation, pose.rotation}, false, 1.0 / 60, error))
					m_Impl->Diagnose(error);
			}
			const uint64_t tileKey = (uint64_t(slot.z) << 32) | slot.x;
			m_Impl->draws.push_back(
			    {slot.owner, tileKey, slot.x, slot.z, slot.lod, slot.mesh, slot.material, slot.world});
		}
		const auto committed = scene.CommitPending();
		if (!committed.IsSuccess()) {
			error = committed.message;
			return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
} /* 命名空间 Catalpa */
