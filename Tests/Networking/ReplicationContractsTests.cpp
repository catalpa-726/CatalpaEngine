#include "catpch.h"

#include "TestRunner.h"
#include "Networking/ReplicationService.h"
#include "Networking/LoopbackSettings.h"
#include "Runtime/Systems/NetworkSystem.h"
#include "Runtime/Systems/NetworkAuthoring.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/NetworkIdentityComponent.h"

namespace Catalpa::Tests {
	namespace {
		class BackpressureTransport final : public INetworkTransport {
		public:
			explicit BackpressureTransport(bool advance) : m_FailAdvance(advance), m_FailSend(!advance) {}

			bool AdvanceTick(uint64_t tick, std::string& error) override {
				if (tick < m_Tick) {
					error = "Backwards tick";
					return false;
				}
				m_Tick = tick;
				if (m_FailAdvance) {
					m_FailAdvance = false;
					error = "Pending queue backpressure";
					return false;
				}
				error.clear();
				return true;
			}

			bool Send(std::span<const std::byte>, std::string& error) override {
				if (m_FailSend) {
					m_FailSend = false;
					error = "Send queue backpressure";
					return false;
				}
				error.clear();
				return true;
			}

			bool Receive(NetworkMessage&, bool& available, std::string& error) override {
				available = false;
				error.clear();
				return true;
			}

			void Close() noexcept override {}

		private:
			uint64_t m_Tick = 0;
			bool m_FailAdvance = false, m_FailSend = false;
		};

		StableId Id(uint64_t value) {
			return StableId::FromWords(0x5245504c54455354ULL, value);
		}

		ReplicatedEntityState Cube() {
			ReplicatedEntityState value;
			value.id = Id(1);
			value.persistent = {Id(2)};
			value.tick = 1;
			value.name = "Cube";
			return value;
		}
	} /* 命名空间 */
	void RunReplicationContractsTests(TestContext& test) {
		std::string error, text;
		auto schema = DefaultReplicationSchema();
		test.Check(
		    ValidateReplicationSchema(schema, error), "Replication schema fixes stable field IDs, types and authority");
		auto badSchema = schema;
		badSchema.fields[1].id = 1;
		test.Check(!ValidateReplicationSchema(badSchema, error), "Duplicate schema fields are rejected");
		/* 先验证复制快照的序列化和损坏数据拒绝路径。 */
		ReplicationSnapshot snapshot;
		snapshot.schema = schema.id;
		snapshot.sender = 1;
		snapshot.tick = 1;
		snapshot.entities.push_back(Cube());
		std::vector<std::byte> bytes;
		test.Check(EncodeReplicationSnapshot(snapshot, bytes, error) && bytes.size() == 154,
		    "Replication uses an explicit little-endian scalar layout without native component padding");
		ReplicationSnapshot restored;
		test.Check(DecodeReplicationSnapshot(bytes, restored, error) &&
		        restored.entities.front().persistent == Cube().persistent,
		    "Snapshot roundtrip preserves persistent and network identities separately");
		auto malformed = bytes;
		malformed.pop_back();
		test.Check(!DecodeReplicationSnapshot(malformed, restored, error) && restored.entities.size() == 1,
		    "Truncated packets preserve the previous decoded snapshot");
		snapshot.entities.push_back(Cube());
		test.Check(!EncodeReplicationSnapshot(snapshot, bytes, error),
		    "Duplicate entities cannot be encoded into one snapshot");
		snapshot.entities.resize(1);
		snapshot.entities[0].local.rotation = {0, 0, 0, 0};
		test.Check(!EncodeReplicationSnapshot(snapshot, bytes, error),
		    "Invalid quaternion fields are rejected rather than normalized on receive");
		/* 在进程内模拟传输条件，随后连接权威端与复制端的场景。 */
		LoopbackSettings settings;
		settings.faults = {2, 5, 7, true};
		test.Check(WriteLoopbackSettings(settings, text, error),
		    "Loopback settings serialize explicit latency/loss/reordering budgets");
		LoopbackSettings roundtrip;
		test.Check(ReadLoopbackSettings(text, roundtrip, error) && roundtrip.faults.reverseDuePackets &&
		        roundtrip.faults.dropEvery == 5,
		    "Loopback settings roundtrip without enabling a real transport");
		test.Check(!ReadLoopbackSettings("{\"schemaVersion\":1,\"transport\":\"udp\"}", roundtrip, error),
		    "Unimplemented cross-machine transports are explicitly rejected");
		for (bool advance : {false, true}) {
			ReplicationService recovery(
			    std::make_unique<NetworkSession>(std::make_unique<BackpressureTransport>(advance), 91), {});
			const auto cube = Cube();
			test.Check(!recovery.Publish(std::span{&cube, 1}, 1, error) && recovery.Receive(error) &&
			        recovery.Publish(std::span{&cube, 1}, 1, error) && recovery.Stats().sent == 1,
			    "Transport backpressure never rewinds the clock or partially publishes replication state");
		}

		std::unique_ptr<INetworkTransport> a, b;
		test.Check(CreateSimulatedLoopbackPair(a, b, settings.faults, error),
		    "Create bounded fault-injected Loopback endpoints");
		ReplicationConfig left, right;
		right.localPeer = 2;
		right.remotePeer = 1;
		auto serverService =
		    std::make_unique<ReplicationService>(std::make_unique<NetworkSession>(std::move(a), 77), left);
		auto clientService =
		    std::make_unique<ReplicationService>(std::make_unique<NetworkSession>(std::move(b), 77), right);
		NetworkSystem server(std::move(serverService)), client(std::move(clientService));
		Scene authority("Authority"), replica("Replica");
		const auto persistent = Cube().persistent;
		auto spawn = [&](uint64_t generation) {
			if (!authority.QueueCreate(persistent, "Cube", {}).IsSuccess() || !authority.CommitPending().IsSuccess())
				return false;
			const auto entity = authority.Resolve(persistent);
			NetworkIdentityComponent identity;
			identity.identity = Id(1);
			identity.generation = generation;
			return QueueDynamicComponent(
			           authority, entity, [entity, identity](World& world) { return world.Add(entity, identity); })
			           .IsSuccess() &&
			    authority.CommitPending().IsSuccess();
		};
		bool healthy = spawn(1);
		uint64_t tick = 0;
		for (; tick < 40 && healthy;) {
			++tick;
			TransformComponent transform;
			healthy = authority.GetTransform(authority.Resolve(persistent), transform).IsSuccess();
			transform.local.translation.x = float(tick);
			healthy = healthy && authority.QueueSetLocal(authority.Resolve(persistent), transform.local).IsSuccess() &&
			    authority.CommitPending().IsSuccess() && server.Tick(authority, tick, error) &&
			    client.Tick(replica, tick, error);
		}
		test.Check(healthy,
		    "Two independent Worlds replicate through delayed, dropped, duplicated and "
		    "reordered byte packets");
		/* 保留第一代实体引用，比较后续同步和重生后的标识。 */
		const auto firstReplica = replica.MakeReference(replica.Resolve(persistent));
		TransformComponent remote;
		test.Check(firstReplica.HasValue() && firstReplica.world != authority.GetWorld().GetIdentity() &&
		        replica.GetTransform(firstReplica.resolved, remote).IsSuccess() && remote.local.translation.x > 20 &&
		        remote.local.translation.x <= 40,
		    "Replica updates its own World identity and converges toward recent authoritative transforms");
		healthy = authority.QueueDestroySubtree(authority.Resolve(persistent)).IsSuccess() &&
		    authority.CommitPending().IsSuccess();
		for (size_t i = 0; i < 20 && healthy; ++i) {
			++tick;
			healthy = server.Tick(authority, tick, error) && client.Tick(replica, tick, error);
		}
		test.Check(healthy && !replica.Resolve(persistent).HasValue() && server.Stats().pendingDespawn == 0,
		    "Explicit despawn tombstones survive loss and retire only after acknowledgement");
		healthy = spawn(2);
		for (size_t i = 0; i < 20 && healthy; ++i) {
			++tick;
			healthy = server.Tick(authority, tick, error) && client.Tick(replica, tick, error);
		}
		const auto second = replica.Resolve(persistent);
		NetworkIdentityComponent identity;
		test.Check(healthy && second.HasValue() && !replica.Resolve(firstReplica).HasValue() &&
		        replica.GetWorld().Get(second, identity).IsSuccess() && identity.generation == 2,
		    "Respawn binds a fresh generation and invalidates the prior runtime entity reference");
		std::map<std::string, ArchiveValue, std::less<>> captured;
		test.Check(CaptureNetworkComponents(authority.GetWorld(), authority.Resolve(persistent), captured, error) &&
		        captured.contains("catalpa.network_identity"),
		    "Network identities survive author snapshots without serializing runtime sessions");
		const auto sourceEntity = authority.Resolve(persistent);
		auto disabled = identity;
		disabled.enabled = false;
		healthy = QueueDynamicComponent(authority, sourceEntity,
		              [sourceEntity, disabled](World& world) { return world.Set(sourceEntity, disabled); })
		              .IsSuccess() &&
		    authority.CommitPending().IsSuccess();
		for (size_t i = 0; i < 20 && healthy; ++i) {
			++tick;
			healthy = server.Tick(authority, tick, error) && client.Tick(replica, tick, error);
		}
		test.Check(healthy && authority.Resolve(persistent) == sourceEntity && !replica.Resolve(persistent).HasValue(),
		    "Stopping replication despawns replicas without deleting the host-authored source entity");

		test.Check(CreateLoopbackPair(a, b, error), "Create isolated authority-check endpoints");
		ReplicationService host(std::make_unique<NetworkSession>(std::move(a), 91), left),
		    guest(std::make_unique<NetworkSession>(std::move(b), 91), right);
		auto owned = Cube();
		owned.owner = 2;
		std::array<ReplicatedEntityState, 1> ownedArray{owned};
		test.Check(host.Publish(ownedArray, 1, error) && guest.Publish({}, 1, error) && guest.Receive(error) &&
		        host.Receive(error),
		    "Server explicitly spawns a client-owned entity");
		owned.name = "Unauthorized rename";
		ownedArray[0] = owned;
		test.Check(
		    guest.Publish(ownedArray, 2, error) && !host.Receive(error) && host.Entities().at(owned.id).name == "Cube",
		    "Owner updates cannot change a server-authoritative name and rejection preserves prior state");
	}
} /* 命名空间 Catalpa::Tests */
