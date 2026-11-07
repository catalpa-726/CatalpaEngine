#include "catpch.h"
#include "AssetPipeline/Animation/AnimationImport.h"
#include "Animation/AnimationAssetLoader.h"
#include <assimp/scene.h>

namespace Catalpa::Tools {
    namespace {
        Math::Mat4 Matrix(const aiMatrix4x4& source) {
            Math::Mat4 result;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    result[c][r] = source[r][c];
            return result;
        }
        bool Product(AssetBuildContext& context, AssetImportResult& result, const std::string& key,
                     const std::string& type, const std::string& name, const std::string& text,
                     std::vector<AssetId> dependencies, std::string& error) {
            if (!context.ReserveWorking(text.size(), error) || !context.WorkspaceFits(text.size(), error))
                return false;
            const auto id = context.Identity(key);
            const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            result.assets.push_back(
                {id, key, name,
                 ImportedAuthorProduct{type, {bytes.begin(), bytes.end()}, std::move(dependencies)}});
            result.mappings.push_back({id, key, name, type});
            return true;
        }
    } // namespace

    bool AppendAssimpAnimations(AssetBuildContext& context, const aiScene& source, ModelAsset& model,
                                AssetImportResult& result, const std::vector<std::vector<uint32_t>>& remaps,
                                std::string& error) try {
        bool needed = source.mNumAnimations > 0;
        for (unsigned i = 0; i < source.mNumMeshes; ++i)
            needed |= source.mMeshes[i]->mNumBones > 0;
        if (!needed)
            return true;
        struct Node {
            const aiNode* source;
            std::string key;
            int32_t parent;
        };
        std::vector<Node> nodes{{source.mRootNode, "node/0", -1}};
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes.size() + 1 > MaximumSkeletonJoints) {
                error = "Animated Assimp hierarchy exceeds 256 joints including the explicit conversion root";
                return false;
            }
            for (unsigned k = 0; k < nodes[i].source->mNumChildren; ++k)
                nodes.push_back(
                    {nodes[i].source->mChildren[k], nodes[i].key + "/" + std::to_string(k), int32_t(i)});
        }
        SkeletonAsset skeleton;
        std::map<std::string, uint32_t> names;
        std::set<std::string> ambiguous;
        for (const auto& node : nodes) {
            const auto id = context.Identity(node.key).value;
            const auto found = std::find_if(model.nodes.begin(), model.nodes.end(),
                                            [&](const auto& value) { return value.id == id; });
            if (found == model.nodes.end()) {
                error = "Animation node lost its stable model identity";
                return false;
            }
            const auto index = uint32_t(skeleton.joints.size());
            const std::string name = node.source->mName.C_Str();
            if (!names.emplace(name, index).second)
                ambiguous.insert(name);
            skeleton.joints.push_back({id, name, node.parent, found->local});
        }
        const auto conversion = context.Identity("conversion-root").value;
        const auto root = std::find_if(model.nodes.begin(), model.nodes.end(),
                                       [&](const auto& value) { return value.id == conversion; });
        if (root == model.nodes.end()) {
            error = "Missing explicit animation conversion root";
            return false;
        }
        for (auto& joint : skeleton.joints)
            if (joint.parent == -1)
                joint.parent = int32_t(nodes.size());
        skeleton.joints.push_back({conversion, root->name, -1, root->local});
        std::vector<Math::TransformTRS> locals;
        for (const auto& joint : skeleton.joints)
            locals.push_back(joint.bindLocal);
        std::vector<Math::Mat4> modelPose;
        if (!BuildModelPose(skeleton, locals, modelPose, error))
            return false;
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            const auto status = Math::InverseAffine(modelPose[i], skeleton.joints[i].inverseBind);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        std::string text;
        if (!WriteSkeleton(skeleton, text, error))
            return false;
        const auto skeletonId = context.Identity("skeleton");
        if (!Product(context, result, "skeleton", "skeleton", "Skeleton", text, {}, error))
            return false;
        model.skeleton.id = skeletonId;
        auto jointFor = [&](const aiString& name) -> uint32_t {
            const std::string key = name.C_Str();
            if (ambiguous.contains(key) || !names.contains(key))
                throw std::invalid_argument("Animation bone/channel name is missing or ambiguous: " + key);
            return names.at(key);
        };

        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
            for (unsigned slot = 0; slot < nodes[nodeIndex].source->mNumMeshes; ++slot) {
                const auto meshIndex = nodes[nodeIndex].source->mMeshes[slot];
                const auto& mesh = *source.mMeshes[meshIndex];
                const auto derivedCount = remaps.at(meshIndex).size();
                if (mesh.mNumVertices > 2000000 || derivedCount > 2000000 ||
                    mesh.mNumBones > MaximumSkeletonJoints ||
                    !context.WorkspaceFits(size_t(mesh.mNumVertices) * (sizeof(SkinInfluence) + 1) +
                                               derivedCount * sizeof(SkinInfluence) * 3,
                                           error)) {
                    if (error.empty())
                        error = "Assimp skin vertex/joint working budget exceeded";
                    return false;
                }
                std::vector<SkinInfluence> original(mesh.mNumVertices);
                std::vector<uint8_t> counts(mesh.mNumVertices, 0);
                std::vector<Math::Mat4> inverseBinds;
                for (const auto& joint : skeleton.joints)
                    inverseBinds.push_back(joint.inverseBind * modelPose[nodeIndex]);
                for (auto& influence : original)
                    influence.weights = {0, 0, 0, 0};
                std::set<uint32_t> boneJoints;
                for (unsigned bi = 0; bi < mesh.mNumBones; ++bi) {
                    const auto& bone = *mesh.mBones[bi];
                    const auto joint = jointFor(bone.mName);
                    if (!boneJoints.insert(joint).second) {
                        error = "Duplicate source bone identity in one mesh";
                        return false;
                    }
                    // Assimp offset 是 mesh local -> joint bind local；不能以名字匹配后忽略其数学约定。
                    inverseBinds[joint] = Matrix(bone.mOffsetMatrix);
                    for (unsigned wi = 0; wi < bone.mNumWeights; ++wi) {
                        const auto& weight = bone.mWeights[wi];
                        if (weight.mVertexId >= original.size() || !std::isfinite(weight.mWeight) ||
                            weight.mWeight < 0 || weight.mWeight > 1) {
                            error = "Invalid source bone influence";
                            return false;
                        }
                        if (weight.mWeight == 0)
                            continue;
                        auto& count = counts[weight.mVertexId];
                        if (count == 4) {
                            error = "More than four positive source weights require an explicit "
                                    "influence-reduction import policy";
                            return false;
                        }
                        auto& influence = original[weight.mVertexId];
                        influence.joints[count] = joint;
                        influence.weights[count++] = weight.mWeight;
                    }
                }
                size_t rigid = 0;
                for (size_t i = 0; i < original.size(); ++i)
                    if (counts[i] == 0) {
                        original[i].joints[0] = uint32_t(nodeIndex);
                        original[i].weights[0] = 1;
                        ++rigid;
                    }
                if (rigid)
                    context.Diagnose(
                        AssetDiagnosticLevel::Info, "animation",
                        "Unweighted vertices use their owning node as an explicit rigid joint: " +
                            std::to_string(rigid));
                MeshSkinBinding binding;
                binding.skeleton = skeletonId;
                binding.mesh = context.Identity("mesh/" + std::to_string(meshIndex));
                binding.meshToSkeleton = modelPose[nodeIndex];
                binding.perJointInverseBind = std::move(inverseBinds);
                for (auto index : remaps.at(meshIndex))
                    binding.influences.push_back(original.at(index));
                if (!EncodeMeshSkinBinding(binding, text, error))
                    return false;
                const auto key = "skin/" + nodes[nodeIndex].key + "/" + std::to_string(slot);
                const auto skinId = context.Identity(key);
                if (!Product(context, result, key, "mesh_skin_binding", mesh.mName.C_Str(), text,
                             {skeletonId, binding.mesh}, error))
                    return false;
                const auto meshNode =
                    context.Identity(nodes[nodeIndex].key + "/mesh/" + std::to_string(slot)).value;
                auto instance =
                    std::find_if(model.meshes.begin(), model.meshes.end(),
                                 [&](const auto& value) { return model.nodes[value.node].id == meshNode; });
                if (instance == model.meshes.end()) {
                    error = "Skin instance lost stable node identity";
                    return false;
                }
                instance->skin.id = skinId;
            }
        for (unsigned ai = 0; ai < source.mNumAnimations; ++ai) {
            const auto& animation = *source.mAnimations[ai];
            if (animation.mNumMeshChannels || animation.mNumMorphMeshChannels ||
                !std::isfinite(animation.mTicksPerSecond) || animation.mTicksPerSecond <= 0) {
                error = "Animation requires explicit positive ticks-per-second and TRS channels; morph/mesh "
                        "channels are unsupported";
                return false;
            }
            AnimationClip clip;
            clip.skeleton = skeletonId;
            clip.duration = animation.mDuration / animation.mTicksPerSecond;
            if (animation.mNumChannels > MaximumSkeletonJoints) {
                error = "Assimp animation channel budget exceeded";
                return false;
            }
            size_t keyCount = 0;
            for (unsigned ci = 0; ci < animation.mNumChannels; ++ci) {
                const auto& channel = *animation.mChannels[ci];
                const size_t count =
                    size_t(channel.mNumPositionKeys) + channel.mNumScalingKeys + channel.mNumRotationKeys;
                if (count > 1000000 - keyCount) {
                    error = "Assimp animation key budget exceeded";
                    return false;
                }
                keyCount += count;
            }
            if (!context.WorkspaceFits(keyCount * sizeof(RotationKeyframe) * 4, error))
                return false;
            for (unsigned ci = 0; ci < animation.mNumChannels; ++ci) {
                const auto& channel = *animation.mChannels[ci];
                AnimationTrack track;
                track.joint = skeleton.joints[jointFor(channel.mNodeName)].id;
                for (unsigned k = 0; k < channel.mNumPositionKeys; ++k) {
                    const auto& key = channel.mPositionKeys[k];
                    track.translation.keys.push_back(
                        {key.mTime / animation.mTicksPerSecond, {key.mValue.x, key.mValue.y, key.mValue.z}});
                }
                for (unsigned k = 0; k < channel.mNumScalingKeys; ++k) {
                    const auto& key = channel.mScalingKeys[k];
                    track.scale.keys.push_back(
                        {key.mTime / animation.mTicksPerSecond, {key.mValue.x, key.mValue.y, key.mValue.z}});
                }
                for (unsigned k = 0; k < channel.mNumRotationKeys; ++k) {
                    const auto& key = channel.mRotationKeys[k];
                    track.rotation.keys.push_back({key.mTime / animation.mTicksPerSecond,
                                                   {key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z}});
                }
                clip.tracks.push_back(std::move(track));
            }
            if (!ValidateAnimationClip(clip, &skeleton, error) || !EncodeAnimationClip(clip, text, error))
                return false;
            const auto key = "animation/" + std::to_string(ai);
            if (!Product(context, result, key, "animation_clip", animation.mName.C_Str(), text, {skeletonId},
                         error))
                return false;
            model.animations.emplace_back(context.Identity(key));
        }
        context.Diagnose(AssetDiagnosticLevel::Info, "animation",
                         "Stable source-node skeleton, per-derived-vertex weights and TRS clips imported "
                         "without modifying source data");
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = "Assimp animation import: " + std::string(exception.what());
        return false;
    }
} // namespace Catalpa::Tools
