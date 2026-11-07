#pragma once
#include "Animation/Skinning.h"
#include "Animation/BlendTreeAsset.h"

namespace Catalpa {
    class AssetManager;
    CATALPA_API bool ReadSkeleton(std::string_view, SkeletonAsset&, std::string& error);
    CATALPA_API bool WriteSkeleton(const SkeletonAsset&, std::string&, std::string& error);
    CATALPA_API bool ReadAnimationClip(std::string_view, AnimationClip&, std::string& error);
    CATALPA_API bool WriteAnimationClip(const AnimationClip&, std::string&, std::string& error);
    CATALPA_API bool EncodeAnimationClip(const AnimationClip&, std::string&, std::string& error);
    CATALPA_API bool ReadMeshSkinBinding(std::string_view, MeshSkinBinding&, std::string& error);
    CATALPA_API bool WriteMeshSkinBinding(const MeshSkinBinding&, std::string&, std::string& error);
    // Cooked 权重用有界二进制流；避免数十万顶点先膨胀为 JSON DOM 再解析。
    CATALPA_API bool EncodeMeshSkinBinding(const MeshSkinBinding&, std::string&, std::string& error);
    CATALPA_API bool ReadBlendTree(std::string_view, BlendTreeAsset&, std::string& error);
    CATALPA_API bool WriteBlendTree(const BlendTreeAsset&, std::string&, std::string& error);
    CATALPA_API bool RegisterAnimationAssetLoaders(AssetManager&, std::string& error);
} // namespace Catalpa
