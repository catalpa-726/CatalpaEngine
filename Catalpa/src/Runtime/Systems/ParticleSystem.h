#pragma once
#include "Particles/ParticleEffect.h"
#include "Scene/EntityReference.h"
namespace Catalpa {
	class Scene;
	class AssetManager;
	struct ParticleEmitterSnapshot {
		EntityReference entity;
		AssetVersionRef<MaterialInstance> material;
		std::vector<ParticleBillboard> particles;
		uint64_t dropped = 0;
	};
	class CATALPA_API ParticleSystem {
	public:
		/**
		 * @brief 更新场景中粒子发射器的运行状态。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] dt 本次模拟或采样步长，单位为秒。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Tick(Scene&, AssetManager&, double dt, std::string& error);
		/**
		 * @brief 收集指定世界中用于渲染的粒子发射器快照。
		 *
		 * @param[in] world 指定需要提取粒子绘制快照的世界。
		 *
		 * @return 当前粒子发射器的状态快照。
		 */
		std::vector<ParticleEmitterSnapshot> Snapshot(uint64_t world) const;
		void ReleaseWorld(uint64_t world) noexcept;
		void Stop() noexcept { m_Instances.clear(); }

	private:
		struct Instance {
			EntityReference entity;
			AssetVersionRef<ParticleEffect> effect;
			ParticlePool pool;
			Math::Mat4 emitter{1};
			uint64_t restart = 0;
			uint32_t seed = 0;
			bool visible = true;
		};
		std::map<std::tuple<uint64_t, uint32_t, uint32_t>, Instance> m_Instances;
	};
} /* 命名空间 Catalpa */
