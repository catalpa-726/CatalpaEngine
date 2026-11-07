#pragma once
#include "Animation/SkeletonAsset.h"

namespace Catalpa {
    enum class AnimationInterpolation { Step, Linear, CubicSpline };
    struct VectorKeyframe {
        double time = 0;
        Math::Vec3 value{0}, inTangent{0}, outTangent{0};
    };
    struct RotationKeyframe {
        double time = 0;
        Math::Quat value{1, 0, 0, 0}, inTangent{0, 0, 0, 0}, outTangent{0, 0, 0, 0};
    };
    template <class Key> struct AnimationChannel {
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<Key> keys;
    };
    struct AnimationTrack {
        StableId joint;
        AnimationChannel<VectorKeyframe> translation, scale;
        AnimationChannel<RotationKeyframe> rotation;
    };
    struct AnimationEvent {
        double time = 0;
        std::string name, payload;
    };
    struct AnimationClip {
        AssetId skeleton;
        AssetVersionRef<SkeletonAsset> skeletonVersion;
        double duration = 1;
        std::vector<AnimationTrack> tracks;
        std::vector<AnimationEvent> events;
        StableId rootMotionJoint;
    };
    struct AnimationPose {
        std::vector<Math::TransformTRS> local;
        std::vector<Math::Mat4> model;
    };
    CATALPA_API bool ValidateAnimationClip(const AnimationClip&, const SkeletonAsset*, std::string& error);
    CATALPA_API bool SampleAnimation(const SkeletonAsset&, const AnimationClip&, double seconds,
                                     AnimationPose&, std::string& error);
    CATALPA_API bool BlendPoses(const SkeletonAsset&, const AnimationPose&, const AnimationPose&,
                                float weight, AnimationPose&, std::string& error);
} // namespace Catalpa
