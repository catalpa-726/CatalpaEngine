#pragma once
#include "Animation/AnimationClip.h"
#include "Geometry/MeshAsset.h"

namespace Catalpa {
    // GPU 可直接消费的 32 字节独立流；顺序是固定 Skeleton 版本的 joints 顺序。
    struct SkinInfluence {
        std::array<uint32_t, 4> joints{};
        std::array<float, 4> weights{1, 0, 0, 0};
    };
    static_assert(sizeof(SkinInfluence) == 32);
    struct MeshSkinBinding {
        AssetId skeleton, mesh;
        AssetVersionRef<SkeletonAsset> skeletonVersion;
        AssetVersionRef<MeshAsset> meshVersion;
        Math::Mat4 meshToSkeleton{1};
        // 可选源 bind-shape：mesh local -> 对应 joint bind local。
        // 为空时使用 inverseBind(skeleton) * meshToSkeleton，允许旧作者资产继续使用统一 bind 姿态。
        std::vector<Math::Mat4> perJointInverseBind;
        std::vector<SkinInfluence> influences;
    };
    CATALPA_API bool ValidateMeshSkinBinding(const MeshSkinBinding&, std::string& error);
    CATALPA_API bool BuildSkinningPalette(const SkeletonAsset&, const AnimationPose&,
                                          const Math::Mat4& meshToSkeleton, std::vector<Math::Mat4>&,
                                          std::string& error);
    CATALPA_API bool BuildSkinningPalette(const MeshSkinBinding&, const AnimationPose&,
                                          std::vector<Math::Mat4>&, std::string& error);
    class CATALPA_API ValidatedSkinEvaluator final {
      public:
        bool Bind(AssetVersionRef<MeshSkinBinding>, std::string& error);
        bool BuildPalette(const AnimationPose&, std::vector<Math::Mat4>&, std::string& error) const;
        const AssetVersionRef<MeshSkinBinding>& Binding() const noexcept {
            return m_Binding;
        }

      private:
        AssetVersionRef<MeshSkinBinding> m_Binding;
        Math::Mat4 m_MeshFromSkeleton{1};
        std::vector<Math::Mat4> m_BindFromMesh;
    };
    // CPU 参考值供 GPU 结果对照；不会代替 SkinningPass，也不修改源网格。
    CATALPA_API bool SkinMeshReference(const MeshData&, std::span<const SkinInfluence>,
                                       std::span<const Math::Mat4> palette, MeshData&, std::string& error);
} // namespace Catalpa
