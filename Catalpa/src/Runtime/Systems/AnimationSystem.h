#pragma once
#include "Animation/AnimationAssetLoader.h"
#include "Scene/EntityReference.h"
namespace Catalpa {
	class Scene;
	class AssetManager;
	struct AnimatedMeshSnapshot {
		EntityReference entity;
		AssetVersionRef<MeshSkinBinding> binding;
		std::vector<Math::Mat4> palette;
		/* 由全部骨骼矩阵变换的源 AABB 求并集，保守覆盖非负权重的线性混合。 */
		Aabb bounds;
	};
	struct AnimationRuntimeEvent {
		EntityReference entity;
		AnimationEvent event;
	};
	class CATALPA_API AnimationSystem final {
	public:
		/**
		 * @brief 为场景中的动画实例更新片段绑定、姿态与播放状态。
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
		 * @brief 收集给定世界的动画网格快照。
		 *
		 * @param[in] world 仅收集该世界中的蒙皮渲染快照。
		 *
		 * @return 当前场景的动画网格快照，包含渲染所需的姿态信息。
		 */
		std::vector<AnimatedMeshSnapshot> Snapshot(uint64_t world) const;
		/**
		 * @brief 取出事件。
		 *
		 * @return 本次操作收集的结果，元素类型为 AnimationRuntimeEvent。
		 */
		std::vector<AnimationRuntimeEvent> TakeEvents();
		/**
		 * @brief 释放世界。
		 *
		 * @param[in] world 标识需要释放动画实例与快照的世界。
		 */
		void ReleaseWorld(uint64_t world) noexcept;
		/**
		 * @brief 取消并等待解码作业，结束当前音频流。
		 */
		void Stop() noexcept {
			m_Instances.clear();
			m_Meshes.clear();
			m_Skins.clear();
			m_Events.clear();
		}

	private:
		struct Instance {
			EntityReference entity;
			AssetId animation;
			uint64_t version = 0;
			AnimatorState player;
			ValidatedClipEvaluator evaluator;
			BlendTreeRuntime tree;
			AnimationPose pose;
			AssetVersionRef<SkeletonAsset> skeleton;
		};
		using Key = std::tuple<uint64_t, uint32_t, uint32_t>;
		std::map<Key, Instance> m_Instances;
		std::map<Key, AnimatedMeshSnapshot> m_Meshes;
		std::map<Key, ValidatedSkinEvaluator> m_Skins;
		std::vector<AnimationRuntimeEvent> m_Events;
	};
} /* 命名空间 Catalpa */
