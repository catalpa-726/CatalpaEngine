#pragma once
#include "Renderer/RenderFramePacket.h"
#include "Particles/ParticleEffect.h"
namespace Catalpa {
	struct RenderParticleEmitter {
		RenderInstanceId owner;
		AssetVersionRef<MaterialInstance> material;
		std::vector<ParticleBillboard> particles;
	};
	/* 生成camera-facing实例，由已有透明排序和材质Pass消费；不另建渲染器。 */
	class ParticlePass {
	public:
		/**
		 * @brief 为面向相机的粒子创建可复用的四边形网格。
		 *
		 * @param[out] output 接收生成的网格版本引用。
		 * @param[out] error 失败时接收原因。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool CreateQuad(AssetVersionRef<MeshAsset>&, std::string&);
		/**
		 * @brief 把粒子转换为面向相机的网格实例并追加到当前帧。
		 *
		 * @param[out] packet 接收粒子实例的场景帧。
		 * @param[in] emitters 提供粒子位置和材质的发射器集合。
		 * @param[in] quad 各粒子共享的四边形网格。
		 * @param[out] error 失败时接收原因。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Append(RenderFramePacket&, std::span<const RenderParticleEmitter>,
		    const AssetVersionRef<MeshAsset>&, std::string&);
	};
} /* 命名空间 Catalpa */
