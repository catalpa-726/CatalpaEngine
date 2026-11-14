#pragma once
#include "Animation/BlendTreeAsset.h"
#include "ECS/World.h"
#include "Core/Memory/ValueMap.h"
namespace Catalpa {
	/* 只存在于编辑器独占预览 Scene；固定草稿及 scrub 时间不进入作者 Scene 的序列化。 */
	struct AnimationPreviewComponent {
		AssetVersionRef<SkeletonAsset> skeleton;
		AssetVersionRef<AnimationClip> clip;
		AssetVersionRef<BlendTreeAsset> tree;
		double time = 0;
		ValueMap<std::string, float> parameters;
	};
	template <> struct ComponentTraits<AnimationPreviewComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
