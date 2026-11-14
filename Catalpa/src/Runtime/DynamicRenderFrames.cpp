#include "catpch.h"

#include "EngineRuntime.h"
#include "Renderer/Passes/ParticlePass.h"
#include "Renderer/Passes/TerrainPass.h"
namespace Catalpa {
	/**
	 * @brief 追加动态帧。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in,out] packet 接收当前场景的动画、粒子和地形渲染数据的帧快照。
	 * @param[in] preview 指示本次提取是否用于预览场景。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool EngineRuntime::AppendDynamicFrame(Scene& scene, RenderFramePacket& packet, bool preview, std::string& error) {
		struct Rollback {
			RenderFramePacket& packet;
			size_t transforms, instances;
			bool committed = false;

			~Rollback() {
				if (!committed) {
					packet.transforms.resize(transforms);
					packet.instances.resize(instances);
				}
			}
		} rollback{packet, packet.transforms.size(), packet.instances.size()};
		const auto world = scene.GetWorld().GetIdentity();
		TerrainService* terrain = &m_Terrain;
		if (preview) {
			auto& owner = m_TerrainPreviews[world];
			if (!owner)
				owner = std::make_unique<TerrainService>();
			terrain = owner.get();
		}
		const auto position = packet.views.empty() ? Math::Vec3{} : packet.views.front().matrices.position;
		PhysicsWorld* physics = nullptr;
		if (!preview && (!m_EditorViewportActive || m_PlayScene.HasValue())) {
			bool hasTerrain = false;
			const auto visited = scene.GetWorld().ForEach([&](EntityId id) {
				hasTerrain = hasTerrain || scene.GetWorld().Has<TerrainComponent>(id);
				return WorldStatus{};
			});
			if (!visited.IsSuccess()) {
				error = visited.message;
				return false;
			}
			if (hasTerrain) {
				if (!m_Physics.EnsureWorld(scene, error))
					return false;
				physics = m_Physics.World();
			}
		}
		if (!terrain->Tick(scene, *m_Assets, m_Jobs, position, physics, error))
			return false;
		std::vector<RenderTerrainTile> tiles;
		for (const auto& tile : terrain->Draws()) {
			const auto id = tile.owner.resolved;
			tiles.push_back({{id.world, id.index, id.generation}, tile.tileKey, tile.mesh, tile.material, tile.world});
		}
		if (!TerrainPass::Append(packet, tiles, error))
			return false;
		if (!m_ParticleQuad.HasValue() && !ParticlePass::CreateQuad(m_ParticleQuad, error))
			return false;
		std::vector<RenderParticleEmitter> particles;
		for (const auto& emitter : m_Particles.Snapshot(world)) {
			const auto id = emitter.entity.resolved;
			particles.push_back({{id.world, id.index, id.generation}, emitter.material, emitter.particles});
		}
		if (!ParticlePass::Append(packet, particles, m_ParticleQuad, error))
			return false;
		rollback.committed = true;
		return true;
	}
} /* 命名空间 Catalpa */
