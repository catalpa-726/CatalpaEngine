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
	/**
	 * @brief 校验混合树。
	 *
	 * @param[in] tree 待检查节点连接、片段依赖和骨架兼容性的混合树。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateBlendTree(const BlendTreeAsset&, std::string& error);
	struct BlendTreeRuntime {
		StableId state, previousState;
		double transitionTime = 0;
		std::map<StableId, AnimatorState> players;
		std::map<StableId, ValidatedClipEvaluator> evaluators;
	};
	/**
	 * @brief 按混合树参数和播放状态求本帧的混合姿态。
	 *
	 * @param[in] tree 已绑定依赖的混合树。
	 * @param[in] overrides 本次覆盖的混合参数值。
	 * @param[in] dt 本次模拟或采样步长，单位为秒。
	 * @param[in] state 在连续求值间保存的节点播放状态。
	 * @param[out] output 接收混合结果。
	 * @param[out] error 失败时接收具体诊断。
	 * @param[in] extractRootMotion 是否提取根运动。
	 * @param[in] playbackSpeed 播放速率倍率。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool EvaluateBlendTree(const BlendTreeAsset&, const std::map<std::string, float, std::less<>>&,
	    double dt, BlendTreeRuntime&, AnimationAdvance&, std::string& error, bool extractRootMotion = false,
	    double playbackSpeed = 1);
} /* 命名空间 Catalpa */
