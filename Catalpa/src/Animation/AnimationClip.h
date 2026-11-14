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
	/**
	 * @brief 校验动画片段。
	 *
	 * @param[in] clip 待检查持续时间、轨道与关键帧的动画片段。
	 * @param[in] skeleton 可选目标骨架，提供时一并核对动画轨道及根运动关节。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateAnimationClip(const AnimationClip&, const SkeletonAsset*, std::string& error);
	/**
	 * @brief 采样动画。
	 *
	 * @param[in] skeleton 提供关节顺序、层级与绑定姿态的采样骨架。
	 * @param[in] clip 在该骨架上求值的动画片段。
	 * @param[in] seconds 以秒计的时间。
	 * @param[out] output 接收 AnimationPose 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool SampleAnimation(
	    const SkeletonAsset&, const AnimationClip&, double seconds, AnimationPose&, std::string& error);
	/**
	 * @brief 混合姿态。
	 *
	 * @param[in] skeleton 定义两个输入姿态关节顺序和父子关系的骨架。
	 * @param[in] a 第一个 AnimationPose 操作数。
	 * @param[in] b 第二个 AnimationPose 操作数。
	 * @param[in] weight 从第一个姿态混合到第二个姿态的权重。
	 * @param[out] out 接收 AnimationPose 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BlendPoses(const SkeletonAsset&, const AnimationPose&, const AnimationPose&, float weight,
	    AnimationPose&, std::string& error);
} /* 命名空间 Catalpa */
