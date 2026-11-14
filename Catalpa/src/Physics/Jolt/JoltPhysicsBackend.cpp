#include "catpch.h"

#include "Physics/PhysicsWorld.h"
#include "Core/Math/TransformMath.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Catalpa {
	namespace {
		JPH::Vec3 V(Math::Vec3 v) {
			return {v.x, v.y, v.z};
		}

		JPH::RVec3 R(Math::Vec3 v) {
			return {v.x, v.y, v.z};
		}

		Math::Vec3 C(JPH::Vec3Arg v) {
			return {v.GetX(), v.GetY(), v.GetZ()};
		}

		JPH::Quat Q(Math::Quat q) {
			return {q.x, q.y, q.z, q.w};
		}

		Math::Quat C(JPH::QuatArg q) {
			return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
		}

		bool Finite(Math::Vec3 v) {
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		bool ValidStep(double seconds) {
			if (!std::isfinite(seconds) || seconds <= 0 || seconds > 0.1)
				return false;
			const float native = float(seconds);
			return native > 0 && 1.0 / double(native) <= double(std::numeric_limits<float>::max());
		}

		bool ValidSegment(Math::Vec3 origin, Math::Vec3 delta) {
			if (!Finite(origin) || !Finite(delta))
				return false;
			const auto end = glm::dvec3(origin) + glm::dvec3(delta);
			return std::max({std::abs(double(origin.x)), std::abs(double(origin.y)), std::abs(double(origin.z)),
			           std::abs(end.x), std::abs(end.y), std::abs(end.z)}) <= 1e6 &&
			    glm::dot(glm::dvec3(delta), glm::dvec3(delta)) > 0;
		}

		bool PoseValid(const PhysicsPose& p) {
			Math::TransformTRS t;
			t.translation = p.position;
			t.rotation = p.rotation;
			return Math::ValidateTRS(t).IsSuccess() &&
			    std::max({std::abs(p.position.x), std::abs(p.position.y), std::abs(p.position.z)}) <= 1e6f;
		}
		std::mutex globalMutex;
		size_t globalUsers = 0;
		std::atomic_uint64_t nextWorld{1};
		struct JoltLifetime {
			bool active = false;

			bool Acquire(std::string& error) {
				std::lock_guard lock(globalMutex);
				if (!globalUsers) {
					if (JPH::Factory::sInstance) {
						error = "Jolt factory belongs to another owner";
						return false;
					}
					JPH::RegisterDefaultAllocator();
					JPH::Factory::sInstance = new JPH::Factory();
					JPH::RegisterTypes();
				}
				++globalUsers;
				active = true;
				return true;
			}

			~JoltLifetime() {
				if (!active)
					return;
				std::lock_guard lock(globalMutex);
				if (!--globalUsers) {
					JPH::UnregisterTypes();
					delete JPH::Factory::sInstance;
					JPH::Factory::sInstance = nullptr;
				}
			}
		};
		class BroadLayers final : public JPH::BroadPhaseLayerInterface {
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer value) const override {
				return JPH::BroadPhaseLayer((value & 16) ? 0 : 1);
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer value) const override {
				return value.GetValue() == 0 ? "Static" : "Moving";
			}
#endif
		};
		class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
				return !(layer & 16) || broad.GetValue() != 0;
			}
		};
		class PairFilter final : public JPH::ObjectLayerPairFilter {
		public:
			std::array<uint16_t, 16> masks{};

			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
				return !((a & 16) && (b & 16)) && (masks[a & 15] & (1u << (b & 15))) &&
				    (masks[b & 15] & (1u << (a & 15)));
			}
		};
		class QueryLayers final : public JPH::ObjectLayerFilter {
		public:
			explicit QueryLayers(uint16_t value) : mask(value) {}

			bool ShouldCollide(JPH::ObjectLayer layer) const override { return (mask & (1u << (layer & 15))) != 0; }
			uint16_t mask;
		};
		class IgnoreBody final : public JPH::BodyFilter {
		public:
			explicit IgnoreBody(JPH::BodyID value) : id(value) {}

			bool ShouldCollide(const JPH::BodyID& body) const override { return body != id; }
			JPH::BodyID id;
		};

		/**
		 * @brief 验证引擎碰撞形状参数并构建其 Jolt 形状。
		 *
		 * @param[in] data 包含形状种类、尺寸或几何的资产。
		 * @param[out] out 接收 Jolt 形状引用。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Shape(const CollisionShapeAsset& data, JPH::RefConst<JPH::Shape>& out, std::string& error) {
			if (!ValidateCollisionShape(data, error))
				return false;
			JPH::ShapeSettings::ShapeResult result;
			switch (data.kind) {
			case CollisionShapeKind::Box:
				result = JPH::BoxShapeSettings(V(data.dimensions),
				    std::min(0.01f, 0.1f * std::min({data.dimensions.x, data.dimensions.y, data.dimensions.z})))
				             .Create();
				break;
			case CollisionShapeKind::Sphere:
				result = JPH::SphereShapeSettings(data.dimensions.x).Create();
				break;
			case CollisionShapeKind::Capsule:
				result = JPH::CapsuleShapeSettings(data.dimensions.y, data.dimensions.x).Create();
				break;
			case CollisionShapeKind::ConvexHull: {
				JPH::Array<JPH::Vec3> points;
				points.reserve(data.vertices.size());
				for (auto v : data.vertices)
					points.push_back(V(v));
				result = JPH::ConvexHullShapeSettings(points, 0.001f).Create();
				break;
			}
			case CollisionShapeKind::TriangleMesh: {
				JPH::VertexList vertices;
				vertices.reserve(data.vertices.size());
				for (auto v : data.vertices)
					vertices.push_back(JPH::Float3(v.x, v.y, v.z));
				JPH::IndexedTriangleList triangles;
				triangles.reserve(data.indices.size() / 3);
				for (size_t i = 0; i < data.indices.size(); i += 3)
					triangles.push_back(
					    JPH::IndexedTriangle(data.indices[i], data.indices[i + 1], data.indices[i + 2], 0));
				JPH::MeshShapeSettings settings;
				settings.mTriangleVertices = std::move(vertices);
				settings.mIndexedTriangles = std::move(triangles);
				result = settings.Create();
				break;
			}
			}
			if (result.HasError()) {
				error.assign(result.GetError().data(), result.GetError().size());
				return false;
			}
			out = result.Get();
			return true;
		}
	} /* 命名空间 */

	struct PhysicsWorld::Impl final : JPH::ContactListener, JPH::CharacterContactListener {
		/* SDK 全局引用最先构造、最后释放。所有碰撞形状、刚体和角色对象都须在其之前销毁。 */
		JoltLifetime lifetime;
		PhysicsWorldConfig config;
		uint64_t identity = 0, nextHandle = 1, tick = 0, dropped = 0;
		std::thread::id owner = std::this_thread::get_id();
		BroadLayers broadLayers;
		BroadFilter broadFilter;
		PairFilter pairFilter;
		std::unique_ptr<JPH::TempAllocatorImpl> scratch;
		std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
		std::unique_ptr<JPH::PhysicsSystem> physics;
		struct Body {
			JPH::BodyID sdk;
			PhysicsBodyDesc desc;
		};
		struct Character {
			JPH::Ref<JPH::CharacterVirtual> sdk;
			CharacterSettings settings;
			EntityReference owner;
		};
		std::map<uint64_t, Body> bodies;
		std::map<uint32_t, uint64_t> bodyKeys;
		std::map<uint64_t, Character> characters;
		JPH::CharacterVsCharacterCollisionSimple characterCollisions;
		struct Pair {
			uint64_t first, second;
			uint32_t subFirst, subSecond;
			bool trigger;
		};
		std::vector<Pair> contacts;
		std::vector<PhysicsContactEvent> events;

		std::vector<Pair> SaveCharacterContacts(uint64_t handle) const {
			std::vector<Pair> saved;
			for (const auto& pair : contacts)
				if (pair.first == handle || pair.second == handle)
					saved.push_back(pair);
			return saved;
		}

		void RestoreCharacterContacts(uint64_t handle, const std::vector<Pair>& saved, size_t eventCount) noexcept {
			std::erase_if(contacts, [&](const auto& pair) { return pair.first == handle || pair.second == handle; });
			/* contacts预留maxContacts，失败尝试不改变其它pair，因此原快照必定仍可无分配放回。 */
			for (const auto& pair : saved)
				contacts.push_back(pair);
			events.resize(eventCount);
		}

		bool Owner(std::string& error) const {
			if (owner != std::this_thread::get_id() || !physics) {
				error = "Physics world requires its initialized owner thread";
				return false;
			}
			return true;
		}

		Body* Find(PhysicsBodyHandle h) {
			auto it = bodies.find(h.value);
			return h.world == identity && it != bodies.end() ? &it->second : nullptr;
		}

		const Body* Find(PhysicsBodyHandle h) const {
			auto it = bodies.find(h.value);
			return h.world == identity && it != bodies.end() ? &it->second : nullptr;
		}

		uint64_t Key(JPH::BodyID id) const {
			const auto it = bodyKeys.find(id.GetIndexAndSequenceNumber());
			return it == bodyKeys.end() ? 0 : it->second;
		}

		void Event(PhysicsEventKind kind, uint64_t a, uint64_t b, bool trigger, Math::Vec3 p = {}, Math::Vec3 n = {}) {
			const auto entity = [&](uint64_t key) -> EntityReference {
				if (const auto i = bodies.find(key); i != bodies.end())
					return i->second.desc.entity;
				if (const auto i = characters.find(key); i != characters.end())
					return i->second.owner;
				return {};
			};
			if ((!bodies.contains(a) && !characters.contains(a)) || (!bodies.contains(b) && !characters.contains(b)))
				return;
			if (events.size() == config.maxEvents) {
				++dropped;
				return;
			}
			events.push_back({kind, tick, entity(a), entity(b), p, n, trigger, {identity, a}, {identity, b}});
		}

		void Contact(PhysicsEventKind kind, const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m) {
			auto first = Key(a.GetID()), second = Key(b.GetID());
			if (!first || !second)
				return;
			const bool trigger = a.IsSensor() || b.IsSensor();
			auto subFirst = m.mSubShapeID1.GetValue(), subSecond = m.mSubShapeID2.GetValue();
			if (first > second) {
				std::swap(first, second);
				std::swap(subFirst, subSecond);
			}
			const bool existingPair = std::any_of(
			    contacts.begin(), contacts.end(), [&](auto p) { return p.first == first && p.second == second; });
			auto it = std::find_if(contacts.begin(), contacts.end(), [&](auto p) {
				return p.first == first && p.second == second && p.subFirst == subFirst && p.subSecond == subSecond;
			});
			if (it == contacts.end()) {
				if (contacts.size() == config.maxContacts) {
					++dropped;
					return;
				}
				contacts.push_back({first, second, subFirst, subSecond, trigger});
				kind = existingPair ? PhysicsEventKind::Persist : PhysicsEventKind::Begin;
			}
			const auto point =
			    m.mRelativeContactPointsOn1.empty() ? C(a.GetPosition()) : C(m.GetWorldSpaceContactPointOn1(0));
			Event(
			    kind, first, second, trigger, point, C(m.mWorldSpaceNormal) * (Key(a.GetID()) == first ? 1.0f : -1.0f));
		}

		void OnContactAdded(
		    const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override {
			Contact(PhysicsEventKind::Begin, a, b, m);
		}

		void OnContactPersisted(
		    const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override {
			Contact(PhysicsEventKind::Persist, a, b, m);
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
			auto a = Key(pair.GetBody1ID()), b = Key(pair.GetBody2ID());
			auto subA = pair.GetSubShapeID1().GetValue(), subB = pair.GetSubShapeID2().GetValue();
			if (a > b) {
				std::swap(a, b);
				std::swap(subA, subB);
			}
			const auto it = std::find_if(contacts.begin(), contacts.end(),
			    [&](auto p) { return p.first == a && p.second == b && p.subFirst == subA && p.subSecond == subB; });
			if (it != contacts.end()) {
				const bool trigger = it->trigger;
				contacts.erase(it);
				if (std::none_of(
				        contacts.begin(), contacts.end(), [&](auto p) { return p.first == a && p.second == b; }))
					Event(PhysicsEventKind::End, a, b, trigger);
			}
		}

		void CharacterContact(
		    PhysicsEventKind kind, const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact) {
			if (contact.mDistance > 0 && !contact.mHadCollision)
				return;
			auto a = character->GetUserData(),
			     b = contact.mCharacterB ? contact.mCharacterB->GetUserData() : Key(contact.mBodyB);
			if (!a || !b)
				return;
			uint32_t subA = 0, subB = contact.mSubShapeIDB.GetValue();
			auto normal = -C(contact.mContactNormal);
			if (a > b) {
				std::swap(a, b);
				std::swap(subA, subB);
				normal = -normal;
			}
			const bool existing =
			    std::any_of(contacts.begin(), contacts.end(), [&](auto p) { return p.first == a && p.second == b; });
			if (std::none_of(contacts.begin(), contacts.end(), [&](auto p) {
				    return p.first == a && p.second == b && p.subFirst == subA && p.subSecond == subB;
			    })) {
				if (contacts.size() == config.maxContacts) {
					++dropped;
					return;
				}
				contacts.push_back({a, b, subA, subB, contact.mIsSensorB});
				kind = existing ? PhysicsEventKind::Persist : PhysicsEventKind::Begin;
			}
			Event(kind, a, b, contact.mIsSensorB, C(contact.mPosition), normal);
		}

		void OnContactAdded(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
		    JPH::CharacterContactSettings&) override {
			CharacterContact(PhysicsEventKind::Begin, character, contact);
		}

		void OnContactPersisted(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
		    JPH::CharacterContactSettings&) override {
			CharacterContact(PhysicsEventKind::Persist, character, contact);
		}

		void OnContactRemoved(
		    const JPH::CharacterVirtual* character, const JPH::BodyID& body, const JPH::SubShapeID& subShape) override {
			auto a = character->GetUserData(), b = Key(body);
			uint32_t subA = 0, subB = subShape.GetValue();
			if (a > b) {
				std::swap(a, b);
				std::swap(subA, subB);
			}
			const auto i = std::find_if(contacts.begin(), contacts.end(),
			    [&](auto p) { return p.first == a && p.second == b && p.subFirst == subA && p.subSecond == subB; });
			if (i != contacts.end()) {
				const bool trigger = i->trigger;
				contacts.erase(i);
				if (std::none_of(
				        contacts.begin(), contacts.end(), [&](auto p) { return p.first == a && p.second == b; }))
					Event(PhysicsEventKind::End, a, b, trigger);
			}
		}

		void OnCharacterContactAdded(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
		    JPH::CharacterContactSettings&) override {
			CharacterContact(PhysicsEventKind::Begin, character, contact);
		}

		void OnCharacterContactPersisted(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
		    JPH::CharacterContactSettings&) override {
			CharacterContact(PhysicsEventKind::Persist, character, contact);
		}

		void OnCharacterContactRemoved(const JPH::CharacterVirtual* character, const JPH::CharacterID& other,
		    const JPH::SubShapeID& subShape) override {
			auto a = character->GetUserData();
			uint64_t b = 0;
			for (const auto& [key, value] : characters)
				if (value.sdk->GetID() == other) {
					b = key;
					break;
				}
			if (!b)
				return;
			uint32_t subA = 0, subB = subShape.GetValue();
			if (a > b) {
				std::swap(a, b);
				std::swap(subA, subB);
			}
			const auto i = std::find_if(contacts.begin(), contacts.end(),
			    [&](auto p) { return p.first == a && p.second == b && p.subFirst == subA && p.subSecond == subB; });
			if (i != contacts.end()) {
				const bool trigger = i->trigger;
				contacts.erase(i);
				if (std::none_of(
				        contacts.begin(), contacts.end(), [&](auto p) { return p.first == a && p.second == b; }))
					Event(PhysicsEventKind::End, a, b, trigger);
			}
		}

		PhysicsQueryHit Hit(JPH::BodyID body, float fraction, Math::Vec3 point, Math::Vec3 normal) const {
			const auto key = Key(body);
			const auto found = bodies.find(key);
			if (found == bodies.end())
				return {};
			return {true, {identity, key}, found->second.desc.entity, point, normal, fraction};
		}

		~Impl() {
			if (physics) {
				for (auto& [key, c] : characters) {
					c.sdk->SetListener(nullptr);
					c.sdk->SetCharacterVsCharacterCollision(nullptr);
				}
				characters.clear();
				for (auto& [key, b] : bodies) {
					physics->GetBodyInterface().RemoveBody(b.sdk);
					physics->GetBodyInterface().DestroyBody(b.sdk);
				}
				bodies.clear();
				bodyKeys.clear();
				physics->SetContactListener(nullptr);
			}
		}
	};

	PhysicsWorld::PhysicsWorld() = default;

	PhysicsWorld::~PhysicsWorld() {
		Shutdown();
	}

	bool ValidatePhysicsWorldConfig(const PhysicsWorldConfig& config, std::string& error) {
		if (config.maxBodies < 1 || config.maxBodies > 65536 || config.maxContacts < 1 || config.maxContacts > 262144 ||
		    config.maxEvents < 1 || config.maxEvents > 262144 || config.scratchBytes < 1024 * 1024 ||
		    config.scratchBytes > 256 * 1024 * 1024 || !Finite(config.gravity) ||
		    std::max({std::abs(config.gravity.x), std::abs(config.gravity.y), std::abs(config.gravity.z)}) > 10000) {
			error = "Invalid physics configuration or repeated initialization";
			return false;
		}
		/*
		 * 本后端使用单线程离散刚体步进。为碰撞对、约束岛和角色的 256 次命中预留临时存储，当前不处理关节或软体。
		 * 交给 Jolt 的固定块必须足够大，避免 Update 内部因临时分配器耗尽而终止进程。
		 */
		const size_t requiredScratch =
		    2 * 1024 * 1024 + size_t(config.maxBodies) * 2048 + size_t(config.maxContacts) * 128;
		if (config.scratchBytes < requiredScratch) {
			error = "Physics scratch is below the configured body/contact capacity reserve";
			return false;
		}
		error.clear();
		return true;
	}

	bool PhysicsWorld::Initialize(const PhysicsWorldConfig& config, std::string& error) try {
		if (m_Impl) {
			error = "Repeated physics world initialization";
			return false;
		}
		if (!ValidatePhysicsWorldConfig(config, error))
			return false;
		auto next = std::make_unique<Impl>();
		next->config = config;
		if (!next->lifetime.Acquire(error))
			return false;
		auto identity = nextWorld.load();
		while (identity != UINT64_MAX && !nextWorld.compare_exchange_weak(identity, identity + 1)) {
		}
		next->identity = identity;
		if (identity == UINT64_MAX) {
			error = "Physics world identities exhausted";
			return false;
		}
		next->pairFilter.masks = config.collisionMasks;
		next->scratch = std::make_unique<JPH::TempAllocatorImpl>(uint32_t(config.scratchBytes));
		/* 同步适配：Jolt任务在当前固定步完成，不创建第二个线程池或后台世界写入者。 */
		next->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
		next->physics = std::make_unique<JPH::PhysicsSystem>();
		next->physics->Init(config.maxBodies, 0, config.maxContacts, config.maxContacts, next->broadLayers,
		    next->broadFilter, next->pairFilter);
		auto settings = next->physics->GetPhysicsSettings();
		settings.mMaxInFlightBodyPairs = int(config.maxContacts);
		settings.mUseLargeIslandSplitter = false;
		next->physics->SetPhysicsSettings(settings);
		next->physics->SetGravity(V(config.gravity));
		next->physics->SetContactListener(next.get());
		next->events.reserve(config.maxEvents);
		next->contacts.reserve(config.maxContacts);
		next->characterCollisions.mCharacters.reserve(config.maxBodies);
		m_Impl = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	void PhysicsWorld::Shutdown() noexcept {
		if (m_Impl && m_Impl->owner != std::this_thread::get_id())
			std::terminate();
		m_Impl.reset();
	}

	bool PhysicsWorld::CreateBody(const PhysicsBodyDesc& desc, const CollisionShapeAsset& shape,
	    const PhysicsMaterialAsset& material, PhysicsBodyHandle& out, std::string& error) try {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (!PoseValid(desc.pose) || !Finite(desc.scale) || desc.scale.x <= 0 || desc.scale.y <= 0 ||
		    desc.scale.z <= 0 || std::max({desc.scale.x, desc.scale.y, desc.scale.z}) > 10000 ||
		    !Finite(desc.linearVelocity) ||
		    std::max({std::abs(desc.linearVelocity.x), std::abs(desc.linearVelocity.y),
		        std::abs(desc.linearVelocity.z)}) > 10000 ||
		    desc.motion > PhysicsMotion::Dynamic || desc.collisionGroup > 15 || !std::isfinite(desc.massKg) ||
		    desc.massKg < 0.001f || desc.massKg > 1e8f || !std::isfinite(desc.gravityFactor) ||
		    desc.gravityFactor < 0 || desc.gravityFactor > 100 || !ValidatePhysicsMaterial(material, error)) {
			error = "Invalid physics body descriptor";
			return false;
		}
		if (shape.kind == CollisionShapeKind::TriangleMesh && desc.motion != PhysicsMotion::Static) {
			error = "Triangle-mesh colliders are static; use a convex hull for moving bodies";
			return false;
		}
		if (m_Impl->bodies.size() + m_Impl->characters.size() >= m_Impl->config.maxBodies ||
		    m_Impl->nextHandle == UINT64_MAX) {
			error = "Physics body budget/identity exhausted";
			return false;
		}
		JPH::RefConst<JPH::Shape> native;
		if (!Shape(shape, native, error))
			return false;
		if (desc.scale != Math::Vec3{1}) {
			const auto scaled = JPH::ScaledShapeSettings(native.GetPtr(), V(desc.scale)).Create();
			if (scaled.HasError()) {
				error.assign(scaled.GetError().data(), scaled.GetError().size());
				return false;
			}
			native = scaled.Get();
		}
		const auto bounds = native->GetLocalBounds();
		if (bounds.mMin.Abs().ReduceMax() > 1e6f || bounds.mMax.Abs().ReduceMax() > 1e6f) {
			error = "Scaled collision shape exceeds the bounded metre-space world";
			return false;
		}
		const auto motion = desc.motion == PhysicsMotion::Static
		    ? JPH::EMotionType::Static
		    : (desc.motion == PhysicsMotion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic);
		JPH::BodyCreationSettings settings(native.GetPtr(), R(desc.pose.position), Q(desc.pose.rotation), motion,
		    JPH::ObjectLayer(desc.collisionGroup | (desc.motion == PhysicsMotion::Static ? 16 : 0)));
		settings.mFriction = material.friction;
		settings.mRestitution = material.restitution;
		settings.mIsSensor = desc.sensor;
		settings.mLinearVelocity = V(desc.linearVelocity);
		settings.mMaxLinearVelocity = 10000;
		settings.mGravityFactor = desc.gravityFactor;
		if (desc.motion == PhysicsMotion::Dynamic) {
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = desc.massKg;
		}
		auto& api = m_Impl->physics->GetBodyInterface();
		auto* body = api.CreateBody(settings);
		if (!body) {
			error = "Jolt body allocation failed";
			return false;
		}
		const auto id = body->GetID();
		const auto key = m_Impl->nextHandle++;
		try {
			m_Impl->bodies.emplace(key, Impl::Body{id, desc});
			m_Impl->bodyKeys.emplace(id.GetIndexAndSequenceNumber(), key);
		} catch (...) {
			m_Impl->bodies.erase(key);
			m_Impl->bodyKeys.erase(id.GetIndexAndSequenceNumber());
			api.DestroyBody(id);
			throw;
		}
		api.AddBody(
		    id, desc.motion == PhysicsMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		out = {m_Impl->identity, key};
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool PhysicsWorld::DestroyBody(PhysicsBodyHandle handle, std::string& error) {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		const auto* body = m_Impl->Find(handle);
		if (!body) {
			error = "Stale or foreign physics body";
			return false;
		}
		for (auto it = m_Impl->contacts.begin(); it != m_Impl->contacts.end();)
			if (it->first == handle.value || it->second == handle.value) {
				const auto pair = *it;
				it = m_Impl->contacts.erase(it);
				if (std::none_of(m_Impl->contacts.begin(), m_Impl->contacts.end(),
				        [&](auto p) { return p.first == pair.first && p.second == pair.second; }))
					m_Impl->Event(PhysicsEventKind::End, pair.first, pair.second, pair.trigger);
			} else
				++it;
		const auto native = body->sdk;
		auto& api = m_Impl->physics->GetBodyInterface();
		api.RemoveBody(native);
		api.DestroyBody(native);
		m_Impl->bodyKeys.erase(native.GetIndexAndSequenceNumber());
		m_Impl->bodies.erase(handle.value);
		error.clear();
		return true;
	}

	bool PhysicsWorld::ReadBody(PhysicsBodyHandle handle, PhysicsBodyState& output, std::string& error) const {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		const auto* body = m_Impl->Find(handle);
		if (!body) {
			error = "Stale or foreign body";
			return false;
		}
		JPH::RVec3 position;
		JPH::Quat rotation;
		const auto& api = m_Impl->physics->GetBodyInterface();
		api.GetPositionAndRotation(body->sdk, position, rotation);
		PhysicsBodyState value{
		    {C(position), C(rotation)}, C(api.GetLinearVelocity(body->sdk)), api.IsActive(body->sdk)};
		if (!PoseValid(value.pose) || !Finite(value.linearVelocity)) {
			error = "Physics output is nonfinite or outside the bounded metre-space world";
			return false;
		}
		output = value;
		error.clear();
		return true;
	}

	bool PhysicsWorld::SetPose(
	    PhysicsBodyHandle handle, const PhysicsPose& pose, bool teleport, double dt, std::string& error) {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		auto* body = m_Impl->Find(handle);
		if (!body || !PoseValid(pose) || (!teleport && !ValidStep(dt))) {
			error = "Invalid body pose or step";
			return false;
		}
		auto& api = m_Impl->physics->GetBodyInterface();
		if (!teleport && body->desc.motion == PhysicsMotion::Dynamic) {
			error = "Dynamic transform is owned by physics; use explicit teleport";
			return false;
		}
		if (!teleport && body->desc.motion == PhysicsMotion::Kinematic) {
			/* Jolt MoveKinematic直接除以dt，不限制所需速度；先验证再写SDK状态。 */
			JPH::RVec3 currentPosition;
			JPH::Quat currentRotation;
			api.GetPositionAndRotation(body->sdk, currentPosition, currentRotation);
			const auto linear = (glm::dvec3(pose.position) - glm::dvec3(C(currentPosition))) / dt;
			const auto rotation = glm::dquat(pose.rotation) * glm::conjugate(glm::dquat(C(currentRotation)));
			const double angularSpeed = 2 * std::acos(std::clamp(std::abs(rotation.w), 0.0, 1.0)) / dt;
			if (!std::isfinite(linear.x) || !std::isfinite(linear.y) || !std::isfinite(linear.z) ||
			    std::max({std::abs(linear.x), std::abs(linear.y), std::abs(linear.z)}) > 10000 ||
			    !std::isfinite(angularSpeed) || angularSpeed > 10000) {
				error = "Kinematic target exceeds the 10000 m/s or rad/s motion bound; use explicit teleport";
				return false;
			}
			api.MoveKinematic(body->sdk, R(pose.position), Q(pose.rotation), float(dt));
		} else
			api.SetPositionAndRotationWhenChanged(body->sdk, R(pose.position), Q(pose.rotation),
			    body->desc.motion == PhysicsMotion::Static ? JPH::EActivation::DontActivate
			                                               : JPH::EActivation::Activate);
		body->desc.pose = pose;
		error.clear();
		return true;
	}

	bool PhysicsWorld::Step(double seconds, uint64_t tick, std::string& error) {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (!ValidStep(seconds) || !tick || tick <= m_Impl->tick) {
			error = "Physics step requires increasing ticks and float-representable seconds in (0,0.1]";
			return false;
		}
		m_Impl->tick = tick;
		const auto result = m_Impl->physics->Update(float(seconds), 1, m_Impl->scratch.get(), m_Impl->jobs.get());
		if (result != JPH::EPhysicsUpdateError::None) {
			error = "Jolt contact/pair budget exhausted";
			return false;
		}
		error.clear();
		return true;
	}

	bool PhysicsWorld::Raycast(Math::Vec3 origin, Math::Vec3 delta, PhysicsQueryHit& out, std::string& error,
	    uint16_t mask, PhysicsBodyHandle ignore) const {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (!ValidSegment(origin, delta)) {
			error = "Ray endpoints must be distinct finite positions within the 1e6 metre world bound";
			return false;
		}
		const auto* ignored = ignore.HasValue() ? m_Impl->Find(ignore) : nullptr;
		if (ignore.HasValue() && !ignored) {
			error = "Ray ignore handle is stale or foreign";
			return false;
		}
		JPH::RayCastResult result;
		const JPH::RRayCast ray(R(origin), V(delta));
		QueryLayers layers(mask);
		IgnoreBody filter(ignored ? ignored->sdk : JPH::BodyID{});
		PhysicsQueryHit value;
		if (m_Impl->physics->GetNarrowPhaseQuery().CastRay(ray, result, {}, layers, filter)) {
			const auto point = ray.GetPointOnRay(result.mFraction);
			JPH::BodyLockRead lock(m_Impl->physics->GetBodyLockInterface(), result.mBodyID);
			if (!lock.Succeeded()) {
				error = "Ray hit body disappeared";
				return false;
			}
			value = m_Impl->Hit(result.mBodyID, result.mFraction, C(point),
			    C(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)));
		}
		out = value;
		error.clear();
		return true;
	}

	bool PhysicsWorld::Sweep(const CollisionShapeAsset& asset, const PhysicsPose& pose, Math::Vec3 delta,
	    PhysicsQueryHit& out, std::string& error, uint16_t mask, PhysicsBodyHandle ignore) const {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (asset.kind == CollisionShapeKind::TriangleMesh) {
			error = "Sweep query shape must be convex; mesh colliders remain supported as targets";
			return false;
		}
		if (!PoseValid(pose) || !ValidSegment(pose.position, delta)) {
			error = "Invalid shape sweep";
			return false;
		}
		const auto* ignored = ignore.HasValue() ? m_Impl->Find(ignore) : nullptr;
		if (ignore.HasValue() && !ignored) {
			error = "Sweep ignore handle is stale";
			return false;
		}
		JPH::RefConst<JPH::Shape> shape;
		if (!Shape(asset, shape, error))
			return false;
		const auto cast = JPH::RShapeCast::sFromWorldTransform(shape.GetPtr(), JPH::Vec3::sReplicate(1),
		    JPH::RMat44::sRotationTranslation(Q(pose.rotation), R(pose.position)), V(delta));
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> hits;
		QueryLayers layers(mask);
		IgnoreBody bodyFilter(ignored ? ignored->sdk : JPH::BodyID{});
		m_Impl->physics->GetNarrowPhaseQuery().CastShape(cast, {}, JPH::RVec3::sZero(), hits, {}, layers, bodyFilter);
		PhysicsQueryHit value;
		if (hits.HadHit()) {
			const auto& hit = hits.mHit;
			const auto normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
			value = m_Impl->Hit(hit.mBodyID2, hit.mFraction, C(hit.mContactPointOn2), C(normal));
		}
		out = value;
		error.clear();
		return true;
	}

	bool PhysicsWorld::Overlap(const CollisionShapeAsset& asset, const PhysicsPose& pose,
	    std::vector<PhysicsQueryHit>& output, std::string& error, uint16_t mask, size_t maxHits) const try {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (asset.kind == CollisionShapeKind::TriangleMesh) {
			error = "Overlap query shape must be convex";
			return false;
		}
		if (!PoseValid(pose) || !maxHits || maxHits > 4096) {
			error = "Invalid overlap pose or budget";
			return false;
		}
		JPH::RefConst<JPH::Shape> shape;
		if (!Shape(asset, shape, error))
			return false;
		class Collector final : public JPH::CollideShapeCollector {
		public:
			explicit Collector(size_t limit) : limit(limit) { values.reserve(limit); }

			void AddHit(const JPH::CollideShapeResult& result) override {
				if (values.size() == limit) {
					overflow = true;
					ForceEarlyOut();
					return;
				}
				values.push_back(result);
			}
			size_t limit;
			bool overflow = false;
			std::vector<JPH::CollideShapeResult> values;
		} hits(maxHits);
		QueryLayers layers(mask);
		m_Impl->physics->GetNarrowPhaseQuery().CollideShape(shape.GetPtr(), JPH::Vec3::sReplicate(1),
		    JPH::RMat44::sRotationTranslation(Q(pose.rotation), R(pose.position))
		        .PreTranslated(shape->GetCenterOfMass()),
		    {}, JPH::RVec3::sZero(), hits, {}, layers);
		if (hits.overflow) {
			error = "Overlap hit budget exhausted";
			return false;
		}
		std::vector<PhysicsQueryHit> values;
		values.reserve(hits.values.size());
		for (const auto& h : hits.values) {
			auto value = m_Impl->Hit(
			    h.mBodyID2, 0, C(h.mContactPointOn2), C(-h.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY())));
			if (value.hit)
				values.push_back(value);
		}
		std::sort(
		    values.begin(), values.end(), [](const auto& a, const auto& b) { return a.body.value < b.body.value; });
		values.erase(
		    std::unique(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.body == b.body; }),
		    values.end());
		output.swap(values);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool PhysicsWorld::CreateCharacter(const CharacterSettings& settings, const PhysicsPose& pose,
	    PhysicsBodyHandle& output, std::string& error, PhysicsBodyHandle replacing, EntityReference owner) try {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		if (m_Impl->config.gravity.x != 0 || m_Impl->config.gravity.z != 0 || m_Impl->config.gravity.y > 0) {
			error = "Y-up character controller requires nonpositive Y-only gravity";
			return false;
		}
		if (replacing.HasValue() &&
		    (replacing.world != m_Impl->identity || !m_Impl->characters.contains(replacing.value))) {
			error = "Character replacement handle is stale or foreign";
			return false;
		}
		if (!PoseValid(pose) || !std::isfinite(settings.radius) || settings.radius < 0.01f || settings.radius > 100 ||
		    !std::isfinite(settings.halfHeight) || settings.halfHeight < 0.01f || settings.halfHeight > 100 ||
		    !std::isfinite(settings.maxSlopeRadians) || settings.maxSlopeRadians < 0 ||
		    settings.maxSlopeRadians >= 1.5707f || !std::isfinite(settings.stepHeight) || settings.stepHeight < 0 ||
		    settings.stepHeight > 10 || !std::isfinite(settings.snapDistance) || settings.snapDistance < 0 ||
		    settings.snapDistance > 10 || !std::isfinite(settings.massKg) || settings.massKg < 0.001f ||
		    settings.massKg > 10000 || settings.collisionGroup > 15 ||
		    m_Impl->characters.size() + m_Impl->bodies.size() >= m_Impl->config.maxBodies ||
		    m_Impl->nextHandle >= UINT32_MAX) {
			error = "Invalid character configuration or budget";
			return false;
		}
		auto capsule = JPH::CapsuleShapeSettings(settings.halfHeight, settings.radius).Create();
		if (capsule.HasError()) {
			error = "Character capsule creation failed";
			return false;
		}
		JPH::CharacterVirtualSettings native;
		native.mShape = capsule.Get();
		native.mMaxSlopeAngle = settings.maxSlopeRadians;
		native.mMass = settings.massKg;
		const auto key = m_Impl->nextHandle++;
		native.mID = JPH::CharacterID(uint32_t(key));
		JPH::Ref<JPH::CharacterVirtual> character =
		    new JPH::CharacterVirtual(&native, R(pose.position), Q(pose.rotation), key, m_Impl->physics.get());
		const auto layer = JPH::ObjectLayer(settings.collisionGroup);
		character->SetCharacterVsCharacterCollision(&m_Impl->characterCollisions);
		m_Impl->characterCollisions.mCharacters.clear();
		for (auto& [key, other] : m_Impl->characters)
			if (key != replacing.value &&
			    m_Impl->pairFilter.ShouldCollide(layer, JPH::ObjectLayer(other.settings.collisionGroup)))
				m_Impl->characterCollisions.Add(other.sdk.GetPtr());
		character->RefreshContacts(m_Impl->physics->GetDefaultBroadPhaseLayerFilter(layer),
		    m_Impl->physics->GetDefaultLayerFilter(layer), {}, {}, *m_Impl->scratch);
		if (character->GetMaxHitsExceeded() ||
		    std::any_of(character->GetActiveContacts().begin(), character->GetActiveContacts().end(),
		        [](const auto& c) { return !c.mIsSensorB && c.mDistance < -0.02f; })) {
			error = "Character has insufficient standing space or contact-query budget";
			return false;
		}
		m_Impl->characters.emplace(key, Impl::Character{character, settings, owner});
		character->SetListener(m_Impl.get());
		output = {m_Impl->identity, key};
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool PhysicsWorld::MoveCharacter(
	    PhysicsBodyHandle handle, Math::Vec3 desired, double dt, CharacterMotion& output, std::string& error) try {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		auto found = m_Impl->characters.find(handle.value);
		if (handle.world != m_Impl->identity || found == m_Impl->characters.end() || !Finite(desired) ||
		    desired.y < 0 || std::max({std::abs(desired.x), std::abs(desired.y), std::abs(desired.z)}) > 10000 ||
		    !ValidStep(dt)) {
			error = "Invalid character handle, velocity or step";
			return false;
		}
		auto& entry = found->second;
		auto& character = *entry.sdk;
		m_Impl->characterCollisions.mCharacters.clear();
		for (auto& [key, other] : m_Impl->characters)
			if (m_Impl->pairFilter.ShouldCollide(
			        JPH::ObjectLayer(entry.settings.collisionGroup), JPH::ObjectLayer(other.settings.collisionGroup)))
				m_Impl->characterCollisions.Add(other.sdk.GetPtr());
		const auto oldPosition = character.GetPosition();
		const auto oldVelocity = character.GetLinearVelocity();
		const auto oldContacts = m_Impl->SaveCharacterContacts(handle.value);
		const auto eventCount = m_Impl->events.size();
		const bool grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
		auto velocity = character.GetLinearVelocity();
		float vertical = velocity.GetY();
		if (grounded && vertical <= 0)
			vertical = character.GetGroundVelocity().GetY();
		if (grounded && desired.y > 0)
			vertical = character.GetGroundVelocity().GetY() + desired.y;
		const auto supportVelocity = grounded ? character.GetGroundVelocity() : JPH::Vec3::sZero();
		velocity = JPH::Vec3(desired.x + supportVelocity.GetX(), vertical + m_Impl->config.gravity.y * float(dt),
		    desired.z + supportVelocity.GetZ());
		character.SetLinearVelocity(velocity);
		JPH::CharacterVirtual::ExtendedUpdateSettings step;
		step.mWalkStairsStepUp = {0, entry.settings.stepHeight, 0};
		step.mStickToFloorStepDown = {0, -entry.settings.snapDistance, 0};
		const auto layer = JPH::ObjectLayer(entry.settings.collisionGroup);
		character.ExtendedUpdate(float(dt), V(m_Impl->config.gravity), step,
		    m_Impl->physics->GetDefaultBroadPhaseLayerFilter(layer), m_Impl->physics->GetDefaultLayerFilter(layer), {},
		    {}, *m_Impl->scratch);
		if (character.GetMaxHitsExceeded()) {
			character.SetListener(nullptr);
			character.SetPosition(oldPosition);
			character.SetLinearVelocity(oldVelocity);
			character.RefreshContacts(m_Impl->physics->GetDefaultBroadPhaseLayerFilter(layer),
			    m_Impl->physics->GetDefaultLayerFilter(layer), {}, {}, *m_Impl->scratch);
			character.SetListener(m_Impl.get());
			m_Impl->RestoreCharacterContacts(handle.value, oldContacts, eventCount);
			error = "Character contact-query budget exhausted; previous position retained";
			return false;
		}
		output = {{C(character.GetPosition()), C(character.GetRotation())}, C(character.GetLinearVelocity()),
		    character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround};
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool PhysicsWorld::TeleportCharacter(PhysicsBodyHandle handle, const PhysicsPose& pose, std::string& error) try {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		auto it = m_Impl->characters.find(handle.value);
		if (handle.world != m_Impl->identity || it == m_Impl->characters.end() || !PoseValid(pose)) {
			error = "Invalid character teleport";
			return false;
		}
		const auto oldPosition = it->second.sdk->GetPosition();
		const auto oldVelocity = it->second.sdk->GetLinearVelocity();
		const auto oldRotation = it->second.sdk->GetRotation();
		const auto oldContacts = m_Impl->SaveCharacterContacts(handle.value);
		const auto eventCount = m_Impl->events.size();
		m_Impl->characterCollisions.mCharacters.clear();
		for (auto& [key, other] : m_Impl->characters)
			if (m_Impl->pairFilter.ShouldCollide(JPH::ObjectLayer(it->second.settings.collisionGroup),
			        JPH::ObjectLayer(other.settings.collisionGroup)))
				m_Impl->characterCollisions.Add(other.sdk.GetPtr());
		it->second.sdk->SetPosition(R(pose.position));
		it->second.sdk->SetRotation(Q(pose.rotation));
		it->second.sdk->SetLinearVelocity(JPH::Vec3::sZero());
		const auto layer = JPH::ObjectLayer(it->second.settings.collisionGroup);
		it->second.sdk->RefreshContacts(m_Impl->physics->GetDefaultBroadPhaseLayerFilter(layer),
		    m_Impl->physics->GetDefaultLayerFilter(layer), {}, {}, *m_Impl->scratch);
		if (it->second.sdk->GetMaxHitsExceeded() ||
		    std::any_of(it->second.sdk->GetActiveContacts().begin(), it->second.sdk->GetActiveContacts().end(),
		        [](const auto& c) { return !c.mIsSensorB && c.mDistance < -0.02f; })) {
			it->second.sdk->SetListener(nullptr);
			it->second.sdk->SetPosition(oldPosition);
			it->second.sdk->SetRotation(oldRotation);
			it->second.sdk->SetLinearVelocity(oldVelocity);
			it->second.sdk->RefreshContacts(m_Impl->physics->GetDefaultBroadPhaseLayerFilter(layer),
			    m_Impl->physics->GetDefaultLayerFilter(layer), {}, {}, *m_Impl->scratch);
			it->second.sdk->SetListener(m_Impl.get());
			m_Impl->RestoreCharacterContacts(handle.value, oldContacts, eventCount);
			error = "Character teleport lacks standing space; previous pose retained";
			return false;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool PhysicsWorld::DestroyCharacter(PhysicsBodyHandle handle, std::string& error) {
		if (!m_Impl || !m_Impl->Owner(error))
			return false;
		auto found = m_Impl->characters.find(handle.value);
		if (handle.world != m_Impl->identity || found == m_Impl->characters.end()) {
			error = "Stale character handle";
			return false;
		}
		for (auto i = m_Impl->contacts.begin(); i != m_Impl->contacts.end();)
			if (i->first == handle.value || i->second == handle.value) {
				const auto pair = *i;
				i = m_Impl->contacts.erase(i);
				if (std::none_of(m_Impl->contacts.begin(), m_Impl->contacts.end(),
				        [&](auto p) { return p.first == pair.first && p.second == pair.second; }))
					m_Impl->Event(PhysicsEventKind::End, pair.first, pair.second, pair.trigger);
			} else
				++i;
		m_Impl->characterCollisions.Remove(found->second.sdk.GetPtr());
		found->second.sdk->SetListener(nullptr);
		m_Impl->characters.erase(found);
		error.clear();
		return true;
	}

	void PhysicsWorld::TakeEvents(std::vector<PhysicsContactEvent>& output) {
		if (!m_Impl)
			return;
		if (m_Impl->owner != std::this_thread::get_id())
			throw std::logic_error("Physics event owner mismatch");
		output.clear();
		output.swap(m_Impl->events);
		m_Impl->events.clear();
		m_Impl->events.reserve(m_Impl->config.maxEvents);
		std::stable_sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
			return std::tie(a.tick, a.firstBody.value, a.secondBody.value, a.kind) <
			    std::tie(b.tick, b.firstBody.value, b.secondBody.value, b.kind);
		});
	}

	uint64_t PhysicsWorld::DroppedEvents() const noexcept {
		return m_Impl ? m_Impl->dropped : 0;
	}
} /* 命名空间 Catalpa */
