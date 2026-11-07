#pragma once
#include "Animation/AnimationPlayer.h"

namespace Catalpa {
    enum class BlendNodeKind { Clip, Blend1D };
    struct BlendTreeNode {
        StableId id;
        BlendNodeKind kind = BlendNodeKind::Clip;
        AssetId clip;
        AssetVersionRef<AnimationClip> clipVersion;
        std::string parameter;
        std::vector<StableId> children;
        std::vector<float> thresholds;
        double speed = 1;
    };
    struct BlendTreeState {
        StableId id, node;
        std::string name;
    };
    struct BlendTreeTransition {
        StableId from, to;
        std::string parameter;
        float threshold = 0;
        bool greaterEqual = true;
        double duration = .2;
    };
    struct BlendTreeAsset {
        AssetId skeleton;
        AssetVersionRef<SkeletonAsset> skeletonVersion;
        std::vector<BlendTreeNode> nodes;
        std::vector<BlendTreeState> states;
        std::vector<BlendTreeTransition> transitions;
        StableId initialState;
        std::map<std::string, float, std::less<>> parameters;
        std::map<StableId, std::pair<float, float>> layout;
    };
    CATALPA_API bool ValidateBlendTree(const BlendTreeAsset&, std::string& error);
    struct BlendTreeRuntime {
        StableId state, previousState;
        double transitionTime = 0;
        std::map<StableId, AnimatorState> players;
        std::map<StableId, ValidatedClipEvaluator> evaluators;
    };
    CATALPA_API bool EvaluateBlendTree(const BlendTreeAsset&,
                                       const std::map<std::string, float, std::less<>>&, double dt,
                                       BlendTreeRuntime&, AnimationAdvance&, std::string& error,
                                       bool extractRootMotion = false, double playbackSpeed = 1);
} // namespace Catalpa
