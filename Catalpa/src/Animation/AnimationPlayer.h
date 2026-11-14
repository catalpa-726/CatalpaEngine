#pragma once
#include "Animation/AnimationClip.h"

namespace Catalpa {
	struct AnimatorState {
		double time = 0;
		uint64_t loopCount = 0;
		bool initialized = false;
	};
	struct AnimationAdvance {
		AnimationPose pose;
		std::vector<AnimationEvent> events;
		Math::Vec3 rootTranslation{0};
		Math::Quat rootRotation{1, 0, 0, 0};
	};
	/**
	 * @brief 根据步长与播放倍率推进动画片段，并采样姿态、事件及可选根运动。
	 * dt 以秒计，循环/事件预算有界。seek/reset 由调用者重置 AnimatorState，避免伪造跨越事件。
	 *
	 * @param[in] skeleton 提供关节层级及绑定姿态的目标骨架。
	 * @param[in] clip 与该骨架匹配、用于本次采样的动画片段。
	 * @param[in] dt 本次模拟步长，以秒计，允许范围为 0 至 1。
	 * @param[in] speed 播放速度倍率，允许范围为 −64 至 64，负值表示倒放。
	 * @param[in] loop 到达片段任一端点后是否循环播放。
	 * @param[in] extractRootMotion 是否提取根运动；片段须已指定根运动关节才会产生该结果。
	 * @param[in,out] state 输入上一时刻的播放状态，成功后更新为本次推进后的状态。
	 * @param[out] output 接收本步采样的姿态、跨越的动画事件和根运动结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 片段校验和本次推进均成功时为 true，否则为 false。
	 */
	CATALPA_API bool AdvanceAnimation(const SkeletonAsset&, const AnimationClip&, double dt, double speed, bool loop,
	    bool extractRootMotion, AnimatorState&, AnimationAdvance&, std::string& error);

	/* 持有不可变的确切版本及受检索引；只有 Bind 能建立上下文，采样没有跳过校验的公共布尔开关。 */
	class CATALPA_API ValidatedClipEvaluator final {
	public:
		/**
		 * @brief 验证片段与骨骼，缓存后续采样所需的轨道到关节索引。
		 *
		 * @param[in] clip 待绑定的动画片段版本。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Bind(AssetVersionRef<AnimationClip>, std::string& error);
		/**
		 * @brief 在指定片段时间采样已绑定的动画姿态。
		 *
		 * @param[in] seconds 采样位置，单位为秒。
		 * @param[out] output 成功时写入关节姿态。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Sample(double seconds, AnimationPose&, std::string& error) const;
		/**
		 * @brief 推进已绑定片段的播放状态并计算当前姿态。
		 *
		 * @param[in] dt 本次模拟或采样步长，单位为秒。
		 * @param[in] speed 播放速率倍率。
		 * @param[in] loop 是否循环播放。
		 * @param[in] extractRootMotion 是否提取根运动。
		 * @param[in,out] state 输入并更新的播放状态。
		 * @param[out] output 接收本次求值结果。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Advance(double dt, double speed, bool loop, bool extractRootMotion, AnimatorState&, AnimationAdvance&,
		    std::string& error) const;
		void Clear() noexcept {
			m_Clip = {};
			m_TrackJoints.clear();
		}
		const AssetVersionRef<AnimationClip>& Clip() const noexcept { return m_Clip; }

	private:
		AssetVersionRef<AnimationClip> m_Clip;
		std::vector<uint32_t> m_TrackJoints;
	};
} /* 命名空间 Catalpa */
