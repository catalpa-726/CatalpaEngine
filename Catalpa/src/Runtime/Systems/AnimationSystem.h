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
        // 由全部骨骼矩阵变换的源 AABB 求并集，保守覆盖非负权重的线性混合。
        Aabb bounds;
    };
    struct AnimationRuntimeEvent {
        EntityReference entity;
        AnimationEvent event;
    };
    class CATALPA_API AnimationSystem final {
      public:
        bool Tick(Scene&, AssetManager&, double dt, std::string& error);
        std::vector<AnimatedMeshSnapshot> Snapshot(uint64_t world) const;
        std::vector<AnimationRuntimeEvent> TakeEvents();
        void ReleaseWorld(uint64_t world) noexcept;
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
} // namespace Catalpa
