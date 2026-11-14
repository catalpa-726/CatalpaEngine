#pragma once
#include "Assets/AssetId.h"
#include "Core/Memory/ValueMap.h"
#include "ECS/World.h"
#include "Animation/BlendTreeAsset.h"
namespace Catalpa {
	struct AnimatorComponent {
		AssetId animation; /* animation_clip 或 blend_tree，由资产类型受检分派。 */
		double speed = 1;
		bool enabled = true, loop = true, rootMotion = false;
		ValueMap<std::string, float> parameters;
		AssetVersionRef<AnimationClip> clipVersion;
		AssetVersionRef<BlendTreeAsset> treeVersion;
		AssetVersionRef<SkeletonAsset> skeletonVersion;
	};
	template <> struct ComponentTraits<AnimatorComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
