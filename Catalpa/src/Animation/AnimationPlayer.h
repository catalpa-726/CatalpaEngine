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
    // dt 以秒计，循环/事件预算有界。seek/reset 由调用者重置 AnimatorState，避免伪造跨越事件。
    CATALPA_API bool AdvanceAnimation(const SkeletonAsset&, const AnimationClip&, double dt, double speed,
                                      bool loop, bool extractRootMotion, AnimatorState&, AnimationAdvance&,
                                      std::string& error);

    // 持有不可变的确切版本及受检索引；只有 Bind 能建立上下文，采样没有跳过校验的公共布尔开关。
    class CATALPA_API ValidatedClipEvaluator final {
      public:
        bool Bind(AssetVersionRef<AnimationClip>, std::string& error);
        bool Sample(double seconds, AnimationPose&, std::string& error) const;
        bool Advance(double dt, double speed, bool loop, bool extractRootMotion, AnimatorState&,
                     AnimationAdvance&, std::string& error) const;
        void Clear() noexcept {
            m_Clip = {};
            m_TrackJoints.clear();
        }
        const AssetVersionRef<AnimationClip>& Clip() const noexcept {
            return m_Clip;
        }

      private:
        AssetVersionRef<AnimationClip> m_Clip;
        std::vector<uint32_t> m_TrackJoints;
    };
} // namespace Catalpa
